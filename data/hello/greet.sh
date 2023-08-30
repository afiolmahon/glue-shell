_greet()
{
  echo "$1 from $2"
}
# When _greet() is invoked by the shell subprocess of whoever parses the json file,
# we will end up calling something like the following at runtime:
_niceGreet "Howdy" "../insert/working/dir/of/bash/subprocess"

_niceGreet()
{
  _greet
  echo "$__greetings from $__location"
}
# When _niceGreet() is invoked by the shell subprocess of whoever parses the json file,
# we will end up calling something like the following at runtime:
__greeting="Howdy"
__location="../insert/working/dir/of/bash/subprocess"
_niceGreet
unset __location
unset __greeting
