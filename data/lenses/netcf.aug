module Netcf =

  (* Map the entire file content into "content"; used for
     reading files in /sys *)
  let id = [ label "content" . store /.*/ . del /[ \t]*\n/ "\n" ]
