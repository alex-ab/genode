set demo_component test/mesa_demo/triangle_gl-21
set demo_binary    triangle_gl-21
set demo_ram_quota 34M
set demo_caps      200

set demo_config {
<config ld_verbose="true">
	<libc stdout="/dev/log" stderr="/dev/log" rtc="/dev/rtc"/>
	<vfs>
		<dir name="dev">
			<log/> <inline name="rtc">2020-01-01 00:00</inline>
		</dir>
	</vfs>
	<arg value="triangle_gl"/>
	<env key="MESA_LOADER_DRIVER_OVERRIDE" value="iris"/>
	<env key="INTEL_DEBUG" value="buf, submit, pc, sync"/>
	<env key="ST_DEBUG" value="mesa,buffer,nir,tgsi,wf,fallback,precompile,gremedy,noreadpixcache"/>
	<env key="MESA_DEBUG" value="flush,context,incomplete_tex,incomplete_fbo"/>
	<env key="MESA_VERBOSE" value="all"/>
<!--
      { "silent", DEBUG_SILENT }, /* turn off debug messages */
      { "flush", DEBUG_ALWAYS_FLUSH }, /* flush after each drawing command */
      { "incomplete_tex", DEBUG_INCOMPLETE_TEXTURE },
      { "incomplete_fbo", DEBUG_INCOMPLETE_FBO },
      { "context", DEBUG_CONTEXT } /* force set GL_CONTEXT_FLAG_DEBUG_BIT flag */
-->
</config>}

set demo_modules {
	triangle_gl-21
}

source ${genode_dir}/repos/libports/run/mesa-21.inc
