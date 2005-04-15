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
#define SERVER_NAME "localhost"
#define SERVER_PORT 8080
#define BUFSIZE 1024
#define SESSION_ID_MAXLEN 64
#define SESSION_FD 5 /* arbitrary choice */
#define SESSION_TIMEOUT_SECS 600

#define PROG_NAME "sd_web"
#define PROG_URL "http://cscott.net/Projects/Sd/"
#define PROTOCOL "HTTP/1.0"
#define RFC1123FMT "%a, %d %b %Y %H:%M:%S GMT"

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
    exit( 1 );
    return 0; // bogus.
}
static int
redirect(FILE *out, char *session, char *cmd) {
    char location[BUFSIZE];
    snprintf(location, BUFSIZE, "Location: http://%s:%d/%s/%s",
	     SERVER_NAME, SERVER_PORT, session, cmd);
    return send_error(out, 303, "See Other", location, "Session needed.");
}


typedef enum session_request_type { NONE, REPLY, NEW, CONNECT, DESTROY };
struct session_request {
    enum session_request_type request;
    char session_id[0];
};
int recv_session_message(int fd, enum session_request_type *request_type,
			 char *session_id, int session_id_maxlen,
			 int *recv_fd) {
    char hdrbuf[sizeof(struct cmsghdr) + sizeof(int)];
    char rcvbuf[sizeof(session_request) + SESSION_ID_MAXLEN + 1];
    struct session_request *req = (struct session_request *) rcvbuf;
    struct iovec riov = { rcvbuf, sizeof(rcvbuf)-1 };
    struct cmsghdr *hdr = (struct cmsghdr *) hdrbuf;
    struct msghdr rmsg;
    int rc;
    rmsg.msg_iov = &riov;
    rmsg.msg_control = hdr;
    do {
	rmsg.msg_iovlen = 1;
	rmsg.msg_controllen = sizeof(hdrbuf);
	rc = recvmsg(fd, &rmsg, 0);
    } while ((rc<0) && (errno==EAGAIN || errno==EINTR));
    if (rc < 0) return rc; // failure.
    // success: write back received stuff.
    if (request_type) {
	*request_type = NONE;
	if (rc >= sizeof(*req))
	    *request_type = req->request;
    }
    if (session_id && session_id_maxlen>0) {
	session_id[0]=0;
	if (rc > sizeof(*req)) {
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
int send_session_message(int fd, enum session_request_type request_type,
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
int ask_mgr_for_session(int session_mgr_fd, char *session_id,
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



/* This function returns (in a forked child process) to create a new
 * session (by executing the continuation).  At that point, file descriptor
 * SESSION_FD (arbitrarily chosen) contains a SOCK_SEQPACKET connection
 * which will be used to receive/reply to clients.
 * The parameter 'session_fd' is a SOCK_SEQPACKET connection which is used
 * to receive/reply to session management requests. */
void session_server(int session_mgr_fd) {
    enum session_request_type request_type;
    char session_id[SESSION_ID_MAXLEN + 1];
    int reply_fd;
    int fds[2]; // new sessions will put their connection fds here.
    int rc, child;

    while (1) {
	// okay, wait for a session request.
	rc = recv_session_message(session_mgr_fd, &request_type,
				  session_id, sizeof(session_id), &reply_fd);
	if (rc<0) {
	    perror("Session manager error");
	    exit(0);
	}
	if (request_type!=DESTROY && reply_fd < 0)
	    continue; // xxx bad message.
	// what type of request is this?
	switch(request_type) {
	case NEW:
	    // create new socketpair
	    rc = socketpair(AF_UNIX, SOCK_SEQPACKET, 0, fds);
	    if (rc < 0) { perror("Can't create socketpair"); exit(1); }
	    // create new session
	    child = fork();
	    if (child==0) goto new_session; // break won't work here.
	    close(fds[0]);
	    // create session name.
	    pid_to_session_name(child, session_id, sizeof(session_id));
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
	// do it again.
    }
    
 new_session:
    // okay, this is the child process.
    dup2(fds[0], SESSION_FD);
    close(fds[0]); // close unneeded file descriptors.
    close(fds[1]);
    close(session_mgr_fd);
    close(reply_fd);
    alarm(SESSION_TIMEOUT_SECS); // schedule cleanup
    return; // execute continuation.
}

int serve_one(int session_mgr_fd, int socket) {
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
    // make sure there's a session identifier.
    if (1 != sscanf(path, "/%[^ /]/%n", session, &n)) {
	// create a new session and redirect to it.
    renew_session:
	sess_fd = ask_mgr_for_session
	    (session_mgr_fd, NULL, session, sizeof(session));
	if (sess_fd < 0)
	    return send_error(in, 404, "Not Found", NULL, "Can't create session.");
	close(sess_fd);
	return redirect(in, session, "");
    }
    cmd = &path[n];
    // look up the session identifier; redirect if not found.
    sess_fd = ask_mgr_for_session(session_mgr_fd, session, NULL, 0);
    if (sess_fd < 0) goto renew_session; // can't find session.
    // send 'cmd' part to the session process; pass over the socket fd for response
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
    setsockopt(ss, SOL_SOCKET, SO_REUSEADDR, NULL, 0);
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
	    /* notify session manager that this child is dead */
	    char session_id[SESSION_ID_MAXLEN+1];
	    pid_to_session_name(child, session_id, sizeof(session_id));
	    send_session_message(session_mgr_fd, DESTROY, session_id, -1);
	}
	/* accept a connection */
	t = sizeof(sa);
	s = accept(ss, (struct sockaddr *) &sa, &addrlen);
	if (s < 0) continue; // yeah, whatever
	if (fork()) {
	    // server: go around to accept new connection
	    close(s);
	    continue;
	}
	close(ss);
	serve_one(session_mgr_fd, s);
	shutdown(s, SHUT_WR);
	close(s);
	exit(0);
    }
}

int main(int argc, char **argv) {
    int fds[2];
    int rc;
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
    // okay, now start the processes which will launch the sessions.
    session_server(fds[0]);
    // okay, if we're here then we need to start the session
    // ...
    // in the message loop:
    int n=0;
    while(1) {	
	char hdrbuf[sizeof(struct cmsghdr) + sizeof(int)];
	char rcvbuf[BUFSIZE];
	struct iovec riov = { rcvbuf, sizeof(rcvbuf)-1 };
	struct cmsghdr *hdr = (struct cmsghdr *) hdrbuf;
	struct msghdr rmsg;
	FILE * out;
	rmsg.msg_iov = &riov;
	rmsg.msg_control = hdr;
	do {
	    rmsg.msg_iovlen = 1;
	    rmsg.msg_controllen = sizeof(hdrbuf);
	    rc = recvmsg(SESSION_FD, &rmsg, 0);
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
	send_headers(out, 200, "Ok", NULL, "text/html", -1, -1);
	fprintf(out, "<html><h1>%d</h1><h2>%s</h2></html>",
		++n, rcvbuf);
	fclose(out);
    }
}
