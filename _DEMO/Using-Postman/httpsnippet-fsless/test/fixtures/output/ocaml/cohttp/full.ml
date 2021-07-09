open Cohttp_lwt_unix
open Cohttp
open Lwt

let uri = Uri.of_string "http://mockbin.com/har?foo=bar&foo=baz&baz=abc&key=value" in
let headers = Header.init ()
  |> fun h -> Header.add h "cookie" "foo=bar; bar=baz"
  |> fun h -> Header.add h "accept" "application/json"
  |> fun h -> Header.add h "content-type" "application/x-www-form-urlencoded"
in
let body = Cohttp_lwt_body.of_string "foo=bar" in

Client.call ~headers ~body `POST uri
>>= fun (res, body_stream) ->
  (* Do stuff with the result *)
