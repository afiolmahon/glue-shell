
_cmake()
{
	eto xc cmake -S ${_gitRoot} -B ${_buildDir} $@ && ln -si ${_buildDir}
}

_cmake_init()
{
	# TODO: conditional arg on DETO_STAGEDIR
	_cmake "-DUSE_CLANG_TIDY=NO" "-DCMAKE_BUILD_TYPE=RelWithDebugInfo" $@
}

echo "hello" 1>&2
exit 1
