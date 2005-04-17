/*
 * sdui-web.cpp - SD web server front-end.
 *
 *    This file copyright (C) 2005  C. Scott Ananian.
 *    Loosely based on sdui-tty.cpp.
 *
 *    This file is part of "Sd".
 *
 *    Sd is free software; you can redistribute it and/or modify it
 *    under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    Sd is distributed in the hope that it will be useful, but WITHOUT
 *    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 *    or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
 *    License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with Sd; if not, write to the Free Software Foundation, Inc.,
 *    59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 *    This is for version 36.
 */

#define UI_VERSION_STRING "0.01"

#include <assert.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <netinet/in.h>

#include "sd.h"
#include "sdwebico.h"
#define SERVER_NAME "localhost"
#define SERVER_PORT 8080
#define BUFSIZE 1024
#define SESSION_ID_MAXLEN 64
#define SESSION_FD 5 /* arbitrary choice */
#define SESSION_TIMEOUT_SECS 600 /* ten minutes */

#define PROG_NAME "sd_web"
#define PROG_URL "http://cscott.net/Projects/Sd/"
#define PROTOCOL "HTTP/1.0"
#define RFC1123FMT "%a, %d %b %Y %H:%M:%S GMT"

/*--------------------------------------------------------------*/
/*  A tiny bit of cryptography, for session management.         */
/*                                                              */
/* This is the XTEA (also called TEAN) cipher, encoding mode    */
/* only, used as a very simple secure hash function.            */
/*    http://www-users.cs.york.ac.uk/~matthew/TEA/              */
static void
xtea(uint32_t *v, uint32_t *k) {
#define TEA_ROUNDS 64
#define DELTA 0x9e3779b9
    uint32_t y=v[0], z=v[1];
    uint32_t limit=DELTA*TEA_ROUNDS, sum=0;
    while (sum!=limit)
	y += ((z<<4 ^ z>>5) + z) ^ (sum + k[sum&3]),
	sum += DELTA,
	z += ((y<<4 ^ y>>5) + y) ^ (sum + k[(sum>>11) & 3]);
    v[0]=y, v[1]=z;
#undef TEA_ROUNDS
#undef DELTA
}
/* This gets an initial key */
static uint32_t SESSION_KEY[4];
static void
init_session_key() {
    FILE *in = fopen("/dev/random", "rb");
    while (1!=fread(SESSION_KEY, sizeof(SESSION_KEY), 1, in))
	;
    fclose(in);
}
/* This is the hash function */
static void
pid_to_session_name(pid_t child, char *session_id, int session_size) {
    const char * code =
	"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz_-";
    uint32_t text[2] = { 0, child };
    uint64_t result;
    int i;
    xtea(text, SESSION_KEY);
    result = text[0]; result<<=32; result|=text[1]; // big endian.
    for (i=0; i < (session_size-1) && result!=0; i++, result>>=6)
	session_id[i] = code[result&63];
    session_id[i]=0;
}
/* we could decode a session_id to get a pid back,
 * but we don't need that, at the moment. */

/*---------------------------------------------------------------------*/
/* Some basic binary tree functions to handle a session database       */
/* Because session_ids are randomly distributed, our tree should stay  */
/* fairly well-balanced.                                               */
typedef struct session_tree {
    struct session_tree *left, *right;
    int fd;
    char session_id[0];
} *session_tree_t;
static session_tree_t session_db = NULL;

static session_tree_t *
find(session_tree_t *tp, char *session_id) {
    session_tree_t t = *tp;
    if (t==NULL) return tp;
    int c = strcmp(session_id, t->session_id);
    return
	(c<0) ? find(&(t->left), session_id) :
	(c>0) ? find(&(t->right), session_id) :
	tp;
}
static void
insert_into_session_table(char *session_id, int fd) {
    session_tree_t *t, nt = (session_tree_t)
	malloc(sizeof(*nt)+strlen(session_id)+1);
    strcpy(nt->session_id, session_id);
    nt->left = nt->right = NULL;
    assert(fd>=0);
    nt->fd = fd;
    t = find(&session_db, session_id);
    if (*t!=NULL) return; // this is a duplicate =(
    *t = nt;
}
static int
lookup_in_session_table(char *session_id) {
    session_tree_t *t = find(&session_db, session_id);
    if (*t==NULL) return -1; // not found =(
    return (*t)->fd;
}
static session_tree_t
delete_highest(session_tree_t *tp) {
    session_tree_t t = *tp;
    if (t->right)
	return delete_highest(&(t->right));
    *tp = t->left;
    return t;
}
static session_tree_t
delete_lowest(session_tree_t *tp) {
    session_tree_t t = *tp;
    if (t->left)
	return delete_lowest(&(t->left));
    *tp = t->right;
    return t;
}
static void
delete_from_session_table(char *session_id) {
    static int toggle = 0;
    session_tree_t t, *tp, tt;
    tp = find(&session_db, session_id);
    t = *tp;
    if (t==NULL) return; // it's already gone (somehow) =(
    // deleting is easy if t->left or t->right is NULL.
    if ((t->left == NULL) || (t->right == NULL)) {
	*tp = (t->left) ? t->left : t->right;
	free(t);
	return;
    }
    // otherwise, we need to find either the highest left or lowest right
    // subkey and move it here.
    if (0==(toggle=!toggle)) // alternate to keep tree balanced
	tt = delete_highest(&(t->left));
    else
	tt = delete_lowest(&(t->right));
    tt->left = t->left;
    tt->right = t->right;
    *tp = tt;
    free(t);
    return;
}

/*-------------------------------------------------------------------*/
/*  HTTP/HTML-related stuff                                          */
static void
send_headers(FILE *out, int status, char* title, char* extra_header,
	     char* mime_type, off_t length, time_t mod ) {
    time_t now;
    char timebuf[100];

    fprintf(out, "%s %d %s\r\n", PROTOCOL, status, title );
    fprintf(out, "Server: %s\r\n", PROG_NAME );
    now = time( (time_t*) 0 );
    strftime( timebuf, sizeof(timebuf), RFC1123FMT, gmtime( &now ) );
    fprintf(out, "Date: %s\r\n", timebuf );
    if ( extra_header != (char*) 0 )
	fprintf(out, "%s\r\n", extra_header );
    if ( mime_type != (char*) 0 )
	fprintf(out, "Content-Type: %s\r\n", mime_type );
    if ( length >= 0 )
	fprintf(out, "Content-Length: %lld\r\n", (int64_t) length );
    if ( mod != (time_t) -1 ) {
	strftime( timebuf, sizeof(timebuf), RFC1123FMT, gmtime( &mod ) );
	fprintf(out, "Last-Modified: %s\r\n", timebuf );
    }
    fprintf(out, "Connection: close\r\n" );
    fprintf(out, "\r\n" );
}
static int
send_error(FILE *out, int status, char* title, char* extra_header, char* text ) {
    send_headers(out, status, title, extra_header, "text/html", -1, -1 );
    fprintf(out, "<HTML><HEAD><TITLE>%d %s</TITLE></HEAD>\n<BODY BGCOLOR=\"#cc9999\"><H4>%d %s</H4>\n", status, title, status, title );
    fprintf(out, "%s\n", text );
    fprintf(out, "<HR>\n<ADDRESS><A HREF=\"%s\">%s</A></ADDRESS>\n</BODY></HTML>\n", PROG_URL, PROG_NAME );
    fflush( out );
    return 0; // bogus.
}
static int
redirect(FILE *out, char *session, char *extra) {
    char location[BUFSIZE];
    snprintf(location, BUFSIZE, "Location: http://%s:%d/%s/%s",
	     SERVER_NAME, SERVER_PORT, session, extra?extra:"");
    return send_error(out, 303, "See Other", location, "Session needed.");
}


/*----------------------------------------------------------------------*/
/* Our session manager.                                                 */

// These are the messages our manager understands.
typedef enum session_request_type { NONE, REPLY, NEW, CONNECT, DESTROY };
struct session_request {
    enum session_request_type request;
    char session_id[0];
};
// Helper function to receive a message, plus an optional file descriptor.
static int
recv_session_message(int fd, enum session_request_type *request_type,
		     char *session_id, int session_id_maxlen, int *recv_fd) {
    char hdrbuf[sizeof(struct cmsghdr) + sizeof(int)];
    char rcvbuf[sizeof(session_request) + SESSION_ID_MAXLEN + 1];
    struct session_request *req = (struct session_request *) rcvbuf;
    struct iovec riov = { rcvbuf, sizeof(rcvbuf)-1 };
    struct cmsghdr *hdr = (struct cmsghdr *) hdrbuf;
    struct msghdr rmsg;
    int rc;
    rmsg.msg_name = NULL;
    rmsg.msg_namelen = 0;
    rmsg.msg_iov = &riov;
    rmsg.msg_control = hdr;
    do {
	rmsg.msg_iovlen = 1;
	rmsg.msg_controllen = sizeof(hdrbuf);
	rc = recvmsg(fd, &rmsg, MSG_WAITALL);
    } while ((rc<0) && (errno==EAGAIN || errno==EINTR));
    if (rc < 0) return rc; // failure.
    // success: write back received stuff.
    if (request_type) {
	*request_type = NONE;
	if (rc >= (int) sizeof(*req))
	    *request_type = req->request;
    }
    if (session_id && session_id_maxlen>0) {
	session_id[0]=0;
	if (rc > (int) sizeof(*req)) {
	    // ensure null-termination
	    req->session_id[rc-sizeof(*req)]=0;
	    strncpy(session_id, req->session_id, session_id_maxlen);
	    session_id[session_id_maxlen-1]=0;
	}
    }
    // did i get a file descriptor? write it back, too.
    if (recv_fd) {
	*recv_fd = -1;
	if (rmsg.msg_controllen == sizeof(hdrbuf) &&
	    hdr->cmsg_len == sizeof(hdrbuf) &&
	    hdr->cmsg_level == SOL_SOCKET &&
	    hdr->cmsg_type == SCM_RIGHTS)
	    *recv_fd = *(int *)CMSG_DATA(hdr);
    }
    // okay, return status.
    return rc;
}
// Helper function to send a message and an optional file descriptor.
static int
send_session_message(int fd, enum session_request_type request_type,
		     char *session_id, int reply_fd) {
    char hdrbuf[sizeof(struct cmsghdr) + sizeof(int)];
    char sndbuf[sizeof(session_request) + SESSION_ID_MAXLEN + 1];
    struct session_request *req = (struct session_request *) sndbuf;
    struct cmsghdr *shdr = (struct cmsghdr *) hdrbuf;
    struct iovec siov;
    struct msghdr smsg;
    smsg.msg_name = NULL;
    smsg.msg_namelen = 0;
    smsg.msg_iov = &siov;
    smsg.msg_iovlen = 1;
    smsg.msg_control = shdr;
    smsg.msg_controllen = shdr->cmsg_len = sizeof(hdrbuf);
    shdr->cmsg_level = SOL_SOCKET;
    shdr->cmsg_type = SCM_RIGHTS;
    req->request = request_type;
    siov.iov_base = req;
    siov.iov_len = sizeof(*req);
    if (session_id) {
	strcpy(req->session_id, session_id);
	siov.iov_len += strlen(req->session_id)+1;
    }
    if (reply_fd < 0)
	smsg.msg_controllen = 0;
    else
	*(int*)CMSG_DATA(shdr) = reply_fd;
    // okay, send this puppy.
    return sendmsg(fd, &smsg, 0);
}
// THIS IS THE MAIN 'CLIENT' INTERFACE TO THE SESSION MANAGER
// Helper function to send a 'give me a session' message, and get back
// the file descriptor we should use to communicate with that session.
static int
ask_mgr_for_session(int session_mgr_fd, char *session_id,
		    char *new_session_id, int new_session_maxlen) {
    int reply_fd;
    int fds[2];
    int rc;

    // make a reply socketpair
    rc = socketpair(AF_UNIX, SOCK_SEQPACKET, 0, fds);
    if (rc < 0) { perror("Can't create socketpair"); exit(1); }
    // send this message to the session server
    send_session_message(session_mgr_fd, session_id ? CONNECT : NEW,
			 session_id, fds[0]);
    close(fds[0]);
    // wait for a response.
    rc = recv_session_message(fds[1], NULL, new_session_id, new_session_maxlen,
			      &reply_fd);
    if (rc<0) {
	perror("Can't read reply from session manager");
	return -1;
    }
    close(fds[1]); // done with this socketpair.
    return reply_fd;
}

// Process a single session manager request.
static int
session_server_do_one(enum session_request_type request_type,
		      char *session_id, int session_id_maxlen,
		      int reply_fd) {
    int rc, fds[2], child;
#if 0
    printf("Session manager says: %s %s %d\n",
	   request_type==NONE ? "NONE" : request_type==REPLY ? "REPLY" :
	   request_type==NEW ? "NEW" : request_type==CONNECT ? "CONNECT" :
	   request_type==DESTROY ? "DESTROY" : "unknown", session_id,
	   reply_fd);
#endif
    assert(reply_fd >= 0 || request_type==DESTROY);
    // what type of request is this?
    switch(request_type) {
    case NEW:
	// create new socketpair
	rc = socketpair(AF_UNIX, SOCK_SEQPACKET, 0, fds);
	if (rc < 0) { perror("Can't create socketpair"); exit(1); }
	// create new session
	child = fork();
	if (child==0) { // okay, in the child process.
	    close(fds[1]);  // close unneeded file descriptors.
	    close(reply_fd);
	    return fds[0]; // this says create the new session on return
	}
	close(fds[0]);
	// create session name.
	pid_to_session_name(child, session_id, session_id_maxlen);
	insert_into_session_table(session_id, fds[1]);
	goto send_response; // we could also safely fall through here.
    case CONNECT:
	// lookup appropriate fd_to_send from session_id
	fds[1] = lookup_in_session_table(session_id);
	if (fds[1] < 0) fds[1]=-1; // can't find this session
    send_response:
	// send back fds[1]
	send_session_message(reply_fd, REPLY, session_id, fds[1]);
	close(reply_fd);
	// okay, done.
	break;
    case DESTROY:
	// this is easy enough:
	fds[1] = lookup_in_session_table(session_id);
	if (fds[1] >= 0) close(fds[1]);
	delete_from_session_table(session_id);
	// no response necessary.
	break;
    }
    return -1; // no new session.
}

// THIS IS THE MAIN ENTRY POINT FOR THE SESSION MANAGER
/* This function returns (in a forked child process) to create a new
 * session (by executing the continuation).  At that point, file descriptor
 * SESSION_FD (arbitrarily chosen) contains a SOCK_SEQPACKET connection
 * which will be used to receive/reply to clients.
 * The parameter 'session_fd' is a SOCK_SEQPACKET connection which is used
 * to receive/reply to session management requests. */
void session_server(int session_mgr_fd) {
    enum session_request_type request_type;
    char session_id[SESSION_ID_MAXLEN + 1];
    int reply_fd, session_fd;
    int rc, child;

    while (1) {
	/* wait for a session request. */
	rc = recv_session_message
	    (session_mgr_fd, &request_type, session_id, sizeof(session_id),
	     &reply_fd);
	if (rc<0) { perror("Session manager error"); exit(0); }
	/* always reap zombies first.  This way we don't inadvertently give
	 * out a fd to a dead session. */
	while (0 < (child=waitpid(0, 0, WNOHANG))) {
	    /* these should all be real sessions, although it would be
	     * harmless even if they weren't. */
	    char nsession_id[SESSION_ID_MAXLEN + 1];
	    pid_to_session_name(child, nsession_id, sizeof(nsession_id));
	    session_server_do_one(DESTROY,nsession_id,sizeof(nsession_id),-1);
	}
	/* okay, now process this request. */
	session_fd = session_server_do_one
	    (request_type, session_id, sizeof(session_id), reply_fd);
	if (session_fd>=0) break; // start new session.
    }
    /* We only get here if we've forked to create a new session. */
    /* We're in the child process here. */
    close(session_mgr_fd); // we don't need this fd any more.
    // make SESSION_FD the way to talk to this session.
    if (session_fd != SESSION_FD) {
	rc = dup2(session_fd, SESSION_FD);
	if (rc<0) { perror("dup2 failed creating new session"); exit(1); }
	close(session_fd);
    }
    alarm(SESSION_TIMEOUT_SECS); // schedule cleanup
    return; // execute continuation.
}

/*-----------------------------------------------------------------------*/
/* HTTP SERVER                                                           */

// parse one http request.
static int
serve_one(int session_mgr_fd, int socket) {
    char line[BUFSIZE], method[BUFSIZE], path[BUFSIZE], protocol[BUFSIZE];
    char session[BUFSIZE], *cmd;
    FILE *in = fdopen(socket, "a+b");
    int sess_fd, n;
    // get incoming request (method, path, and protocol)
    if (NULL==fgets(line, sizeof(line), in))
	return send_error(in, 400, "Bad Request", NULL, "No request found.");
    if (3 != sscanf(line, "%[^ ] %[^ ] %[^ ]", method, path, protocol ))
	return send_error(in, 400, "Bad Request", NULL, "Can't parse request.");
    // gobble up any additional arguments.
    while (NULL != fgets(line, sizeof(line), in)) {
	if (0==strcmp(line,"\n")) break;
	if (0==strcmp(line,"\r\n")) break;
    }
    // okay, we only support 'get'
    if (strcasecmp(method, "GET")!=0)
	return send_error(in, 501, "Not Implemented", NULL,
			  "Only GET is supported.");
    if (path[0]!='/')
	return send_error(in, 400, "Bad Request", NULL,
			  "Path must start with slash.");
    // some special files
    if (strcmp(path, "/favicon.ico")==0) {
	send_headers(in, 200, "Ok", NULL, "image/x-icon",
		     sizeof(sdweb_ico), -1);
	fwrite(sdweb_ico, sizeof(sdweb_ico), 1, in);
	fclose(in);
	exit(0);
    }
    if (strcmp(path, "/stylesheet.css")==0)
	return send_error(in, 404, "Not Found", NULL, "No stylesheet yet.");
    // make sure there's a session identifier.
    if (1 != sscanf(path, "/%[^ /]/%n", session, &n)) {
	// create a new session and redirect to it.
    renew_session:
	sess_fd = ask_mgr_for_session
	    (session_mgr_fd, NULL, session, sizeof(session));
	if (sess_fd < 0)
	    return send_error(in, 404, "Not Found", NULL, "Can't create session.");
	close(sess_fd);
	return redirect(in, session, "#cursor");
    }
    // parse command.
    if (strncmp(path+n, "c?i=", 4)==0) {
	cmd = path+n+4;
	// XXX need to urldecode this string.
    } else
	cmd = "";
    // look up the session identifier; redirect if not found.
    sess_fd = ask_mgr_for_session(session_mgr_fd, session, NULL, 0);
    if (sess_fd < 0) goto renew_session; // can't find session.
    // send 'cmd' part to the session process; pass the socket fd for response
    // (in client process, call 'alarm' to automagically clean up)
    char hdrbuf[sizeof(struct cmsghdr) + sizeof(int)];
    struct cmsghdr *hdr = (struct cmsghdr *) hdrbuf;
    struct iovec siov = { cmd, strlen(cmd)+1 };
    struct msghdr smsg;
    smsg.msg_name = NULL;
    smsg.msg_namelen = 0;
    smsg.msg_iov = &siov;
    smsg.msg_iovlen = 1;
    smsg.msg_control = hdr;
    smsg.msg_controllen = hdr->cmsg_len = sizeof(hdrbuf);
    hdr->cmsg_level = SOL_SOCKET;
    hdr->cmsg_type = SCM_RIGHTS;
    *(int*)CMSG_DATA(hdr) = socket;
    sendmsg(sess_fd, &smsg, 0);
    close(sess_fd);
    close(socket);
    exit(0); // this client is done.
}

// THIS IS THE MAIN ENTRY POINT FOR THE HTTP SERVER
// Main HTTP server method.  Never returns.
void master_server(unsigned short port, int session_mgr_fd) {
    struct sockaddr_in sa;
    int ss, s, t=1;
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
    sa.sin_port = htons(port);
    sa.sin_family = AF_INET;
    ss = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ss < 0) {
	perror("Can't create master server socket"); exit(1);
    }
    setsockopt(ss, SOL_SOCKET, SO_REUSEADDR, &t, sizeof(t));
    setsockopt(ss, SOL_SOCKET, SO_KEEPALIVE , &t, sizeof(t));
    if ((0 != bind(ss, (struct sockaddr *) &sa, sizeof(sa))) ||
	(0 != listen(ss, 10))) {
	perror("Can't bind or listen to master server socket"); exit(1);
    }
    /* main server loop */
    while (1) {
	socklen_t addrlen = sizeof(sa);
	pid_t child;
	/* reap zombies */
	while (0 < (child=waitpid(0, 0, WNOHANG))) {
	    /* hmm, nothing to do here, i think.  sessions are taken
	     * care of elsewhere. These are just client children. 
	     * Reaping is taken care of by the waitpid call. */
	}
	/* accept a connection */
	t = sizeof(sa);
	s = accept(ss, (struct sockaddr *) &sa, &addrlen);
	if (s < 0) continue; // yeah, whatever
	if (0==fork()) break; // create child process.
	// go around to accept new connection
	close(s);
    }
    // in the child.
    close(ss);
    serve_one(session_mgr_fd, s);
    shutdown(s, SHUT_WR);
    close(s);
    exit(0);
}

/*----------------------------------------------------------------------*/
/* SD UI CODE! (finally!)                                               */
static void wait_for_command(char *command, int command_len);
class ioweb : public iofull { // allow subclassing methods in iofull
public:
    int session_mgr_fd;
    ioweb(int session_mgr_fd):session_mgr_fd(session_mgr_fd) { }
    bool ioweb::init_step(init_callback_state s, int n);
};
// linebuffer abstraction: lines stored in reverse order.
static struct linebuffer {
    struct linebuffer *next;
    const char line[0];
} *linebuffer;
static void linebuffer_add(const char *line) {
    struct linebuffer *lb = (struct linebuffer*)
	malloc(sizeof(*lb)+strlen(line)+1);
    lb->next = linebuffer;
    strcpy((char*)lb->line, line);//ignore 'const' during initialization
    linebuffer = lb;
}
static void linebuffer_delete(int n) {
    struct linebuffer *lb;
    for ( ; linebuffer && n > 0; n--) {
	lb = linebuffer;
	linebuffer = lb->next;
	free(lb);
    }
}
static void linebuffer_emit(struct linebuffer *lb, FILE *out) {
    if (lb==NULL) return; // done.
    linebuffer_emit(lb->next, out);
    fputs(lb->line, out);
}
    
bool ioweb::init_step(init_callback_state s, int n) {
    switch(s) {
    default:
	return iofull::init_step(s, n);
    }
}
void iofull::display_help() { assert(0); }
bool iofull::help_manual() { assert(0); return false; }
bool iofull::help_faq() { assert(0); return false; }

int get_char() { assert(0); return 0; }
void ttu_bell() { return; }

static const char *web_title = NULL, *web_pick_string = NULL;
void ttu_set_window_title(const char *str) {
    if (web_title) free((void*)web_title);
    web_title = strdup(str);
}
void iofull::set_pick_string(const char *str) {
    if (web_pick_string) free((void*)web_pick_string);
    if (str && *str) web_pick_string = strdup(str);
    else web_pick_string = NULL;
}
void iofull::final_initialize()
{
   if (!sdtty_no_console)
      ui_options.use_escapes_for_drawing_people = 1;
   ui_options.diagnostic_mode=true; // sets 'match_lines' to very high value
}
void ttu_initialize() { /* do nothing, for now */ }
void ttu_terminate() { /* nothing to tear down */ }
int get_lines_for_more() { return 20000; /* effectively infinite */ }
void erase_last_n(int n) {
    if (linebuffer==NULL) return;
    if (linebuffer->line[0]==0) n++; // empty 'line in progress' doesn't count
    linebuffer_delete(n);
    if (n>0) // regenerate 'line in progress'
	linebuffer_add("");
}
void clear_line() {
    if (linebuffer==NULL) return;
    if (linebuffer->line[0]==0) return; // no 'line in progress'
    linebuffer_delete(1);
    linebuffer_add(""); // regenerate 'line in progress'
}
void put_line(const char the_line[]) {
    // the 'top' line on the linebuffer is never \n terminated; the others
    // will always be.
    const char *existing = (linebuffer) ? linebuffer->line : "";
    char buffer[strlen(the_line)+strlen(existing)+1];
    const char *start=the_line, *p;
    int i=0;
    strcpy(buffer, existing);
    i+=strlen(existing);
    if (linebuffer) linebuffer_delete(1); // pop existing.
    for (p=start; *p; p++) {
	if (*p=='\r') continue; // ignore these.
	buffer[i++] = *p;
	if (*p=='\n') {
	    buffer[i++]=0;
	    linebuffer_add(buffer);
	    i=0;
	}
    }
    buffer[i++]=0;
    linebuffer_add(buffer);
}
void put_char(int c) {
    char str[] = { c, 0 };
    put_line(str);
}
void rubout() {
    if (linebuffer==NULL) return;
    if (linebuffer->line[0]==0) { linebuffer_delete(1); rubout(); return; }
    ((char*)linebuffer->line)[strlen(linebuffer->line)-1] = 0;
}

// call this to ensure we've started the session server by this point.
// (ie we should do this before we take user input)
void
ensure_session_server() {
    static bool started = false;
    if (!started) {
	started = true;
	session_server(((ioweb*)gg)->session_mgr_fd);
    }
}
void get_string(char *dest, int max) {
    ensure_session_server();
    wait_for_command(dest, max);
    // echo this string.
    put_line(dest);
    put_line("\n");
}

/* web server event loop. */
static void
wait_for_command(char *command, int command_len) {
    while(1) {	
	char hdrbuf[sizeof(struct cmsghdr) + sizeof(int)];
	char rcvbuf[BUFSIZE];
	struct iovec riov = { rcvbuf, sizeof(rcvbuf)-1 };
	struct cmsghdr *hdr = (struct cmsghdr *) hdrbuf;
	struct msghdr rmsg;
	FILE * out;
	int rc;
	rmsg.msg_iov = &riov;
	rmsg.msg_control = hdr;
	do {
	    rmsg.msg_iovlen = 1;
	    rmsg.msg_controllen = sizeof(hdrbuf);
	    rc = recvmsg(SESSION_FD, &rmsg, MSG_WAITALL);
	} while ((rc<0) && (errno==EAGAIN || errno==EINTR));
	if (rc < 0) {
	    perror("Client session error.");
	    exit(1);
	}
	// ensure command is null-terminated.
	rcvbuf[rc]=0;
	// other checks
	if (rmsg.msg_controllen != sizeof(hdrbuf) ||
	    hdr->cmsg_len != sizeof(hdrbuf) ||
	    hdr->cmsg_level != SOL_SOCKET ||
	    hdr->cmsg_type != SCM_RIGHTS)
	    assert(0); // xxx complain
	// reset the alarm -- cleanup later.
	alarm(SESSION_TIMEOUT_SECS);
	// this is where to send the result.
	out = fdopen(*(int *)CMSG_DATA(hdr), "w");
	// did we get a command?
	if (rcvbuf[0]) {
	    char session_id[SESSION_ID_MAXLEN];
	    pid_to_session_name(getpid(), session_id, sizeof(session_id));
	    strncpy(command, rcvbuf, command_len);
	    command[command_len-1]=0;
	    // send a redirect.
	    redirect(out, session_id, "#cursor");
	    fclose(out);
	    return;
	}
	// ok, just a refresh.  emit the transcript.
	send_headers(out, 200, "Ok", NULL, "text/html", -1, -1);
	fprintf
	    (out, "<html><head><meta http-equiv=\"content-type\" "
	     "content=\"text/html; charset=UTF-8\">"
	     "<title>%s%s%s</title>"
	     "<link href=/stylesheet.css rel=stylesheet type=\"text/css\">"
	     "<link rel=icon href=/favicon.ico>"
	     "<link rel=\"SHORTCUT ICON\" href=/favicon.ico>"
	     "<script>\n"
	     "<!--\n"
	     "function sf(){document.c.i.focus();}\n"
	     "// -->\n"
	     "</script></head>"
	     "<body bgcolor=#ffffff text=#000000 onLoad=sf()>"
	     "<form method=get action=c name=c class=sd>"
	     "<pre>",
	     web_title ? web_title : "SD (web edition)",
	     web_pick_string ? ": " : "",
	     web_pick_string ? web_pick_string : "");
	linebuffer_emit(linebuffer, out);
	fprintf
	    (out, "<a name=cursor></a><input type=text size=40 name=i>"
	     "</pre>"
	     "</form>"
	     "</body></html>");
	fclose(out);
	// okay, let's field another request.
    }
}

/*----------------------------------------------------------------------*/
/* Tie it all together with a main() method.                            */


int main(int argc, char **argv) {
    int fds[2];
    int rc;
    // initialize session key
    init_session_key();
    // create session manager socket
    rc = socketpair(AF_UNIX, SOCK_SEQPACKET, 0, fds);
    if (rc < 0) {
	perror("Can't create session manager socketpair");
	exit(1);
    }
    // go off and launch the master server.
    if (0==fork()) {
	close(fds[0]);
	master_server(SERVER_PORT, fds[1]);
	// this will never return, but still.
	exit(0);
    }
    close(fds[1]);
    // okay, now let's do some sd initialization.
    ui_options.reverse_video = true;
    ui_options.pastel_color = true;
    ui_options.no_graphics = 2;
    ioweb ggg(fds[0]);
    gg = &ggg;

    return sdmain(argc, argv);
}
