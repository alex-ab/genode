
if {[have_spec linux] && $use_i965} {
	puts "i965 driver not supported on Linux."
	exit 1
}

if {[have_include power_on/qemu] && $use_i965} {
	puts "i965 driver not supported in Qemu."
	exit 1
}

if {$use_i965} {
	set backend_library egl_i965.lib.so
} else {
	set backend_library egl_swrast.lib.so
}

set driver_config "drivers.config"
if {[have_board pc]} {
	set driver_config "drivers_platform.config"
}

create_boot_directory
import_from_depot [depot_user]/src/[base_src] \
                  [depot_user]/pkg/[drivers_interactive_pkg] \
                  [depot_user]/src/nitpicker \
                  [depot_user]/src/demo \
                  [depot_user]/src/init


set build_components {
	drivers/gpu/intel
	lib/mesa/swrast
}

lappend_if $use_i965 build_components lib/mesa/i965
lappend build_components $demo_component 
build $build_components

set config {
<config prio_levels="4">
	<parent-provides>
		<service name="CPU"/>
		<service name="IRQ"/>
		<service name="IO_MEM"/>
		<service name="IO_PORT"/>
		<service name="LOG"/>
		<service name="PD"/>
		<service name="RM"/>
		<service name="ROM"/>
	</parent-provides>
	<default caps="100"/>
	<default-route>
		<any-service> <parent/> <any-child/> </any-service>
	</default-route>

	<start name="timer">
		<resource name="RAM" quantum="1M"/>
		<provides><service name="Timer"/></provides>
	</start>}

append config {
	<start name="drivers" caps="1500" managing_system="yes" priority="-1">
		<resource name="RAM" quantum="64M"/>
		<provides>
			<service name="Platform"/>
		</provides>
		<binary name="init"/>
		<route>
			<service name="ROM" label="config"> <parent label="} $driver_config {"/> </service>
			<service name="Timer">   <child name="timer"/> </service>
			<service name="Capture"> <child name="nitpicker"/> </service>
			<service name="Event">   <child name="nitpicker"/> </service>
			<any-service> <parent/> </any-service>
		</route>
	</start>

	<start name="nitpicker" caps="200" priority="-1">
		<resource name="RAM" quantum="2M"/>
		<provides>
			<service name="Gui"/> <service name="Capture"/> <service name="Event"/>
		</provides>
		<config>
			<capture/> <event/>
			<domain name="pointer" layer="1" content="client" label="no" origin="pointer" />
			<domain name="default" layer="2" content="client" label="no" focus="click" hover="always" />
			<policy label_prefix="pointer" domain="pointer"/>
			<default-policy                domain="default"/>
		</config>
		<route><any-service><parent/><any-child/></any-service></route>
	</start>
	<start name="pointer" priority="-1">
		<resource name="RAM" quantum="1M"/>
	</start>}

append_if $use_i965 config {
	<start name="intel_fb_drv" caps="2000" priority="-1">
		<binary name="intel_gpu_drv"/>
		<resource name="RAM" quantum="40M"/>
		<provides> <service name="Gpu"/> </provides>
		<route>
			<any-service> <parent /> <any-child/> </any-service>
		</route>
	</start>}

set launchpad_config ""
append launchpad_config {
<config>
	<launcher name="init.} $demo_binary {" ram_quota="1G" caps="2000">
		<binary name="init"/>
		<config verbose="yes">
			<parent-provides>
				<service name="ROM"/>
				<service name="IRQ"/>
				<service name="IO_MEM"/>
				<service name="IO_PORT"/>
				<service name="PD"/>
				<service name="RM"/>
				<service name="CPU"/>
				<service name="LOG"/>
				<service name="Gui"/>
				<service name="Timer"/>
				<service name="Gpu"/>
			</parent-provides>

			<start name="liquid_fb" caps="200">
				<resource name="RAM" quantum="16M"/>
				<provides> <service name="Framebuffer"/> <service name="Input"/> </provides>
				<config resize_handle="off" animate="off" title="gears" width="600" height="600" xpos="100" ypos="100"/>
				<route>
				<any-service> <parent/> <any-child/> </any-service>
				</route>
			</start>}

append launchpad_config {
			<start name="} $demo_binary {" caps="} $demo_caps {">
				<resource name="RAM" quantum="} $demo_ram_quota {"/>}

append launchpad_config $demo_config

append launchpad_config {
				<route>
					<service name="ROM" label="egl_drv.lib.so">
						<parent label="} $backend_library {"/>
					</service>
					<any-service> <parent/> <any-child/> </any-service>
				</route>
			</start>
			</config>
	</launcher>
</config>}

append config {
	<start name="launchpad" caps="60000" priority="-2">
		<resource name="RAM" quantum="32G"/>}
append config $launchpad_config
append config {
		<route>
			<service name="ROM" label="init.} $demo_binary {">
				<parent label="init"/>
			</service>
			<any-service> <parent /> <any-child/> </any-service>
		</route>
	</start>}


append config {
</config>}

install_config $config

set boot_modules {
	libc.lib.so vfs.lib.so libm.lib.so
	egl.lib.so mesa.lib.so stdcxx.lib.so
	expat.lib.so glapi.lib.so

	egl_swrast.lib.so
}

append_if $use_i965 boot_modules {
	egl_i965.lib.so drm.lib.so
	intel_gpu_drv
}

append boot_modules $demo_modules
build_boot_image $boot_modules

append qemu_args " -m 2048"
# limit resolution
append qemu_args " -device VGA,vgamem_mb=4 "

run_genode_until forever
