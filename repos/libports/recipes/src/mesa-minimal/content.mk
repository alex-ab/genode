all: content
	rm -rf generated/.git

MIRROR_FROM_REP_DIR := \
                       lib/mk/egl-minimal.mk \
                       lib/mk/mesa_gpu-softpipe.mk \
                       lib/mk/iris_gen.inc \
                       lib/mk/isl_gen.inc \
                       lib/mk/glapi-minimal.mk \
                       lib/mk/mesa-minimal.inc \
                       lib/mk/mesa-minimal.mk \
                       lib/mk/mesa_api-minimal.mk \
                       lib/mk/mesa-common.inc \
                       lib/mk/softpipe.mk \
                       lib/mk/spec/arm_v8/etnaviv.mk \
                       lib/mk/spec/arm_v8/lima.mk \
                       lib/mk/spec/arm_v8/mesa-minimal.mk \
                       lib/mk/spec/arm_v8/mesa_gpu-etnaviv.mk \
                       lib/mk/spec/arm_v8/mesa_gpu-lima.mk \
                       lib/mk/spec/x86/mesa_gpu-iris.mk \
                       lib/mk/spec/x86/iris.mk \
                       lib/mk/spec/x86/iris_gen110.mk \
                       lib/mk/spec/x86/iris_gen120.mk \
                       lib/mk/spec/x86/iris_gen125.mk \
                       lib/mk/spec/x86/iris_gen200.mk \
                       lib/mk/spec/x86/iris_gen80.mk \
                       lib/mk/spec/x86/iris_gen90.mk \
                       lib/mk/spec/x86/isl_gen110.mk \
                       lib/mk/spec/x86/isl_gen120.mk \
                       lib/mk/spec/x86/isl_gen125.mk \
                       lib/mk/spec/x86/isl_gen200.mk \
                       lib/mk/spec/x86/isl_gen80.mk \
                       lib/mk/spec/x86/isl_gen90.mk \
                       lib/mk/spec/x86_64/mesa-minimal.mk \
                       src/lib/mesa

content: $(MIRROR_FROM_REP_DIR)

$(MIRROR_FROM_REP_DIR):
	$(mirror_from_rep_dir)

PORT_DIR := $(call port_dir,$(REP_DIR)/ports/mesa-minimal)

MIRROR_FROM_PORT_DIR := src/lib/mesa/src generated

content: $(MIRROR_FROM_PORT_DIR)

$(MIRROR_FROM_PORT_DIR):
	mkdir -p $(dir $@)
	cp -r $(PORT_DIR)/$@ $(dir $@)

content: LICENSE

LICENSE:
	cp $(PORT_DIR)/src/lib/mesa/docs/license.rst $@
