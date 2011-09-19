(*
Module: Routes
  Parses /etc/sysconfig/network/routes
*)
module Routes =
autoload xfm

let comment = Util.comment
let empty = Util.empty
let eol = Util.eol | Util.comment

(* Anything that's not a separator is part of a token *)
let tok_ch = /[^ \t\n#\\",]|\\\\[^ \t\n]/
let indent = Util.del_ws " "

let token = store tok_ch+
let route_token = /[0-9]+[.][0-9]+[.][0-9]+[.][0-9]+/
let mask_token = /[0-9]+/

let column(n:string) = [ label n . token ]

let default_route = [  key /default/ . indent . column "gateway"  . indent . column "netmask" . indent . column "device" . eol ]
let route_entry = [  key route_token . del "/" "/" . [ key mask_token . indent . column "gateway"  . indent . column "netmask" . indent . column "device" . eol ] ]

let lns = (comment|empty| default_route | route_entry )*

let filter = incl "/etc/sysconfig/network/routes" .
  Util.stdexcl

let xfm = transform lns filter
