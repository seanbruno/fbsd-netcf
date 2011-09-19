(*
Module: Persist_Net_Rules
  Parses /etc/udev/rules.d/70-persistent-net.rules
*)
module Persist_Net_Rules =
autoload xfm

let comment = Util.comment
let empty = Util.empty
let eol = Util.eol | Util.comment

(* A separator is either whitespace or \ followed by newline *)
let sep_ch = /[ \t]|\\\\\n/
(* Anything that's not a separator is part of a token *)
let tok_ch = /[^ \t\n#\\",]|\\\\[^ \t\n]/
let optok_ch = /==/
let optok = del optok_ch "=="
let eq_optok_ch = /=/
let eq_optok = del eq_optok_ch "="
let indent = Util.del_opt_ws ""
let comma_ch = /,/
let commatok (n:string) = indent . del n n . indent

let token = store tok_ch+
let key_token = key /[a-z0-9]/+
let quote_ch = /"/
let quote = del quote_ch "\""

let name_arg (n:string) = Util.del_str n . indent . eq_optok . indent . quote . key_token . quote
let arg (n:string) = [ label n . Util.del_str n . indent . optok . indent . quote . token . quote ]

let entry = [ arg "SUBSYSTEM" . commatok "," .
		arg "ACTION" . commatok "," .
		arg "DRIVERS" . commatok "," .
		arg "ATTR{address}" . commatok "," .
		arg "ATTR{dev_id}" . commatok "," .
		arg "ATTR{type}" . commatok "," .
		arg "KERNEL" . commatok "," .
		name_arg "NAME" .  eol ]

let lns = (comment|empty|entry)*

let filter = incl "/etc/udev/rules.d/70-persistent-net.rules" .
  Util.stdexcl

let xfm = transform lns filter
