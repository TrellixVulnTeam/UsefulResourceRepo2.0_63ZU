open Cohttp_lwt_unix
open Cohttp
open Lwt

let uri = Uri.of_string "http://mockbin.com/har" in
let headers = Header.init ()
  |> fun h -> Header.add h "cookie" "foo=bar; bar=baz"
in

Client.call ~headers `POST uri
>>= fun (res, body_stream) ->
  (* Do stuff with the result *)
