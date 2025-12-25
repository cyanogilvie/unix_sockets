#include "tclstuff.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <string.h>
#include <stddef.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

/* Tcl 9 removed CONST* macros entirely. I'm redefining them here
  for the unlikely corner case where Tcl8 was build with USE_NON_CONST.
  These definitions are needed in uds.h unless CONST symbols are dropped */

#ifndef CONST
#  define CONST const
#endif
#ifndef CONST84
#  define CONST84 const
#endif
#ifndef CONST86
#  define CONST86 const
#endif
#ifndef CONST84_RETURN
#  define CONST84_RETURN const
#endif

#include "uds.h"
#include "config.h"

//#include <sys/ioctl.h>

/* tcl_const_compat.h */
#include <tcl.h>

#ifndef TCL_SIZE_MAX
# define Tcl_GetSizeIntFromObj Tcl_GetIntFromObj
# define TCL_SIZE_MAX      INT_MAX
# ifndef Tcl_Size
    typedef int Tcl_Size;
# endif
# define TCL_SIZE_MODIFIER ""
#endif

static int close2Proc(void* cdata, Tcl_Interp* interp, int flags);

#if TCL_MAJOR_VERSION >= 9
#  define UNIX_SOCKET_CLOSEPROC  NULL
#else
#  define UNIX_SOCKET_CLOSEPROC  TCL_CLOSE2PROC
#endif

static Tcl_ChannelType unix_socket_channel_type = {
	"unix_socket",
	TCL_CHANNEL_VERSION_5,
	UNIX_SOCKET_CLOSEPROC,
	inputProc,
	outputProc,
	NULL,	//seekProc				// NULLable
	NULL,	//setOptionProc			// NULLable
	NULL,	//getOptionProc			// NULLable
	watchProc,
	getHandleProc,
	close2Proc,
	blockModeProc,					// NULLable
	NULL,	//flushProc				// NULLable
	NULL,	//handlerProc			// NULLable
	NULL,	//wideSeekProc			// NULLable if seekProc is NULL
	NULL,	//threadActionProc		// NULLable
	NULL,	//truncateProc			// NULLable
};

typedef struct uds_state {
	Tcl_Interp *	interp;
	Tcl_Channel		channel;
	char			name[64];
	int				fd;
	Tcl_Obj *		accept_handler;
	Tcl_Obj *		path;
} uds_state;

//>>>
static int close2Proc(ClientData cdata,Tcl_Interp* interp,int flags) //<<<
{
	uds_state *	con = (uds_state *)cdata;

	//fprintf(stderr, "closeProc(%s)\n", con->name);

	Tcl_DeleteFileHandler(con->fd);

	if (con->accept_handler != NULL) {
		unlink(Tcl_GetString(con->path));
		Tcl_DecrRefCount(con->accept_handler);
		con->accept_handler = NULL;
	}

	if (con->path != NULL) {
		Tcl_DecrRefCount(con->path);
	}

	close(con->fd);

	ckfree((char *)con); con = NULL;

	return 0;
}

//>>>
static int inputProc(ClientData cdata, char* buf, int bufSize, int* errorCodePtr) //<<<
{
	int			got;
	uds_state *	con = (uds_state *)cdata;

	got = read(con->fd, buf, bufSize);

	if (got == -1)
		*errorCodePtr = errno;

	return got;
}

//>>>
static int outputProc(ClientData cdata,const char* buf, int toWrite, int* errorCodePtr) //<<<
{
	int			wrote;
	uds_state *	con = (uds_state *)cdata;

	wrote = send(con->fd, buf, (size_t)toWrite, 0);
	if (wrote == -1)
		*errorCodePtr = errno;

	return wrote;
}

//>>>
static int blockModeProc(ClientData cdata,int mode) //<<<
{
	uds_state *	con = (uds_state *)cdata;
	int			flags, err;

	flags = 0;
	err = fcntl(con->fd, F_GETFL, &flags);
	if (err == -1) return errno;
#ifdef O_DELAY
	flags &= ~O_NDELAY;
#endif

	if (mode == TCL_MODE_BLOCKING) {
		flags &= ~O_NONBLOCK;
	} else {
		flags |= O_NONBLOCK;
	}

	err = fcntl(con->fd, F_SETFL, flags);
	if (err == -1) {
		return Tcl_GetErrno();
	}
	/*
	ioctl(con->fd, FIONBIO, 1);
	*/

	return 0;
}

//>>>
static void watchProc(ClientData cdata, int mask) //<<<
{
	uds_state *	con = (uds_state *)cdata;

	if (mask) {
		Tcl_CreateFileHandler(con->fd, mask, (Tcl_FileProc *)Tcl_NotifyChannel,
				(ClientData)con->channel);
	} else {
		Tcl_DeleteFileHandler(con->fd);
	}
}

//>>>
static int getHandleProc(ClientData cdata,int direction,ClientData* handlePtr) //<<<
{
	uds_state *	con = (uds_state *)cdata;

	ptrdiff_t	cd = con->fd;
	*handlePtr = (ClientData)cd;

	return TCL_OK;
}

//>>>
static void accept_dispatcher(ClientData cdata,int mask) //<<<
{
	uds_state *			state = (uds_state *)cdata;
	struct sockaddr_un	client_address;
	int					client_sockfd;
	int					res;
	socklen_t			client_len;
	char				channel_name[64];
	Tcl_Obj *			handler;
	Tcl_Channel			channel;
	uds_state *			con;

	//fprintf(stderr, "accept_dispatcher for %s\n", state->name);
	client_len = sizeof(client_address);	// SUN_LEN() ?
	client_sockfd = accept(state->fd,
			(struct sockaddr *)&client_address, &client_len);
	sprintf(channel_name, "unix_socket%d", client_sockfd);

	// Stop this fd from being inherited by children
	(void)fcntl(client_sockfd, F_SETFD, FD_CLOEXEC);
	con = (uds_state *)ckalloc(sizeof(uds_state));
	channel = Tcl_CreateChannel(&unix_socket_channel_type, channel_name,
			(ClientData)con, (TCL_READABLE | TCL_WRITABLE));
	con->interp = NULL;
	memcpy(con->name, channel_name, 64);
	con->channel = channel;
	con->fd = client_sockfd;
	con->accept_handler = NULL;
	con->path = NULL;

	Tcl_RegisterChannel(state->interp, channel);

	handler	= Tcl_DuplicateObj(state->accept_handler);
	if (Tcl_ListObjAppendElement(state->interp, handler, Tcl_NewStringObj(channel_name, -1)) != TCL_OK) {
		Tcl_BackgroundError(state->interp);
		close(con->fd);
		ckfree((char *)con); con = NULL;
		return;
	}

	//fprintf(stderr, "accept_dispatcher calling script accept handler:\n%s\n",
	//		Tcl_GetString(handler));
	Tcl_IncrRefCount(handler);
	res = Tcl_EvalObjEx(state->interp, handler, TCL_EVAL_GLOBAL);
	Tcl_DecrRefCount(handler);

	if (res != TCL_OK) Tcl_BackgroundError(state->interp);
}

//>>>
static int glue_listen(ClientData cdata,Tcl_Interp* interp,int objc,Tcl_Obj *CONST objv[]) //<<<
{
	int					server_sockfd, server_len;
	struct sockaddr_un	server_address;
	char *				path;
	Tcl_Size			path_len;
	uds_state *			state;
	char				channel_name[64];
	Tcl_Channel			channel;

	CHECK_ARGS(2, "path accept_handler");
	path = Tcl_GetStringFromObj(objv[1], &path_len);
	// Hey, I didn't write it.
	if (path_len > 107) THROW_ERROR("path cannot exceed 107 characters");

	server_sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
	server_address.sun_family = AF_UNIX;
	strncpy(server_address.sun_path, path, 107);
	server_len = sizeof(server_address);		// should this be SUN_LEN()?
	unlink(server_address.sun_path);
	bind(server_sockfd, (struct sockaddr *)&server_address, server_len);
	listen(server_sockfd, 100);		// 100 is max outstanding connection queue

	state = (uds_state *)ckalloc(sizeof(uds_state));

	sprintf(channel_name, "unix_socket%d", server_sockfd);
	channel = Tcl_CreateChannel(&unix_socket_channel_type, channel_name,
			(ClientData)state, 0);

	state->interp = interp;
	state->fd = server_sockfd;
	state->channel = channel;
	memcpy(state->name, channel_name, 64);
	state->accept_handler = objv[2];
	state->path = objv[1];
	Tcl_IncrRefCount(state->accept_handler);
	Tcl_IncrRefCount(state->path);

	Tcl_RegisterChannel(interp, channel);

	Tcl_CreateFileHandler(state->fd, TCL_READABLE, accept_dispatcher,
			(ClientData)state);

	Tcl_SetObjResult(interp, Tcl_NewStringObj(channel_name, -1));

	//fprintf(stderr, "listen: %s\n", channel_name);
	return TCL_OK;
}

//>>>
static int glue_connect(ClientData cdata,Tcl_Interp* interp,int objc,Tcl_Obj *CONST objv[]) //<<<
{
	Tcl_Channel			channel;
	int					fd;
	char				channel_name[64];
	char *				path;
	Tcl_Size			path_len;
    int                 result;
	struct sockaddr_un	address;
	int					sockaddr_len;
	uds_state *			con;

	CHECK_ARGS(1, "path");
	path = Tcl_GetStringFromObj(objv[1], &path_len);
	// Hey, I didn't write it.
	if (path_len > 107) THROW_ERROR("path cannot exceed 107 characters");

	fd = socket(AF_UNIX, SOCK_STREAM, 0);

	// Stop this fd from being inherited by children
	(void)fcntl(fd, F_SETFD, FD_CLOEXEC);

	address.sun_family = AF_UNIX;
	strcpy(address.sun_path, path);
	sockaddr_len = sizeof(address);
	result = connect(fd, (struct sockaddr *)&address, sockaddr_len);
	if (result == -1)
    {
        int error_code = errno;
		THROW_ERROR("Could not connect to unix socket: \"", path, "\"", "(", strerror(error_code), ")");
    }
	sprintf(channel_name, "unix_socket%d", fd);

	con = (uds_state *)ckalloc(sizeof(uds_state));
	channel = Tcl_CreateChannel(&unix_socket_channel_type, channel_name,
			(ClientData)con, (TCL_READABLE | TCL_WRITABLE));
	con->interp = NULL;
	con->channel = channel;
	memcpy(con->name, channel_name, 64);
	con->fd = fd;
	con->accept_handler = NULL;
	con->path = objv[1];
	Tcl_IncrRefCount(con->path);

	Tcl_RegisterChannel(interp, channel);

	Tcl_SetObjResult(interp, Tcl_NewStringObj(channel_name, -1));

	//fprintf(stderr, "connect: %s\n", channel_name);

	return TCL_OK;
}

//>>>
int Unix_sockets_Init(Tcl_Interp *interp) //<<<
{
    /* Notice that the upper bound is exclusive */
	if (Tcl_InitStubs(interp, "8.2-10", 0) == NULL)
    {
		return TCL_ERROR;
    }

	NEW_CMD("unix_sockets::listen", glue_listen);
	NEW_CMD("unix_sockets::connect", glue_connect);

	TEST_OK(Tcl_PkgProvide(interp, PACKAGE_NAME, PACKAGE_VERSION));

	return TCL_OK;
}

//>>>

// vim: foldmethod=marker foldmarker=<<<,>>> ts=4 shiftwidth=4
