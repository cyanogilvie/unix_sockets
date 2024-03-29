#!/usr/bin/env tclsh
#
# modified version of server.tcl for the purpose of having a
# more accurate handling of Tcl asynchronous I/O
#
#  -- fine grained control of events and error condition
#  -- server orderly shuts down when the string "EXIT" is received
#     generating events on the client side that need to be handled
#     (see examples/client-asynch.tcl)
#

lappend auto_path .

package require unix_sockets

proc readable {con} {

    if {[chan eof $con]} {
        puts "eof detected on channel $con"
        chan close $con
        return
    }

    if {[catch {gets $con msg} e einfo]} {

        puts stderr "error detected on 'gets <channel>': $e"
        chan close $con

    } elseif {$e > 0}  {

	    puts stderr "Got $e chars in message \"$msg\" from $con, echoing"
        chan puts $con $msg
        chan flush $con

        # this command shuts down the server
        if {$msg eq "EXIT"} {
            incr ::wait_for_events
        }


    } else {

        # this might happen when the client issues a chan close command
        # but no data were in the socket buffer

        puts stderr "empty line on read, ignoring"
    }
}

proc accept {con} {
	puts stderr "Accepting connection on $con"
    chan event $con readable [list readable $con]
}


set listen [unix_sockets::listen /tmp/example.socket accept]

puts stderr "server listening on socket '$listen'"

vwait ::wait_for_events

puts "server shut down"

chan close $listen
