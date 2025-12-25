#ifndef _UDS_H
#define _UDS_H

static int close2Proc(ClientData cdata, Tcl_Interp *interp, int flags);
static int inputProc(ClientData cdata, char *buf, int bufSize, int *errorCodePtr);
static int outputProc(ClientData cdata, const char *buf, int toWrite, int *errorCodePtr);
static int blockModeProc(ClientData cdata, int mode);
static void watchProc(ClientData cdata, int mask);
static int getHandleProc(ClientData cdata, int direction, ClientData *handlePtr);
static void accept_dispatcher(ClientData cdata, int mask);
static int glue_listen(ClientData cdata, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[]);
static int glue_connect(ClientData cdata, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[]);

#endif
