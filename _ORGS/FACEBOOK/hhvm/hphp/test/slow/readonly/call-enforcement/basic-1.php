<?hh

<<file: __EnableUnstableFeatures('readonly')>>

class C {}

function f($x) { echo "in f\n"; }
function g(readonly $x, $y) { echo "in g\n"; }
function h(...$x) { echo "in h\n"; }

<<__EntryPoint>>
function main() {
  $mut = new C();
  $ro = readonly new C();

  f($mut);
  try { f($ro); } catch (Exception $e) { echo $e->getMessage()."\n"; }

  g($mut, $mut);
  g($ro, $mut);
  try { g($mut, $ro); } catch (Exception $e) { echo $e->getMessage()."\n"; }
  try { g($ro, $ro); } catch (Exception $e) { echo $e->getMessage()."\n"; }

  try { h($ro, $ro); } catch (Exception $e) { echo $e->getMessage()."\n"; }
}
