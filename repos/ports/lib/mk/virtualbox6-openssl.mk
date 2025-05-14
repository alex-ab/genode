include $(REP_DIR)/lib/mk/virtualbox6-common.inc

OPENSSL_DIR  = $(VIRTUALBOX_DIR)/src/libs/openssl-3.3.2

INC_DIR += $(VIRTUALBOX_DIR)/src/libs/zlib-1.2.13

INC_DIR += $(OPENSSL_DIR)
INC_DIR += $(OPENSSL_DIR)/include
INC_DIR += $(OPENSSL_DIR)/gen-includes
INC_DIR += $(OPENSSL_DIR)/providers/common/include
INC_DIR += $(OPENSSL_DIR)/providers/implementations/include

INC_DIR += $(OPENSSL_DIR)/crypto/comp
SRC_C += $(REP_DIR)/src/virtualbox6/dummy_openssl.c

CRYPTO_SRC_CAST = \
	c_cfb64.c \
	c_ecb.c \
	c_enc.c \
	c_ofb64.c \
	c_skey.c

CRYPTO_SRC_RSA = \
	rsa_ameth.c \
	rsa_asn1.c \
	rsa_backend.c \
	rsa_chk.c \
	rsa_crpt.c \
	rsa_depr.c \
	rsa_err.c \
	rsa_gen.c \
	rsa_lib.c \
	rsa_meth.c \
	rsa_mp.c \
	rsa_mp_names.c \
	rsa_none.c \
	rsa_oaep.c \
	rsa_ossl.c \
	rsa_pk1.c \
	rsa_pmeth.c \
	rsa_prn.c \
	rsa_pss.c \
	rsa_saos.c \
	rsa_schemes.c \
	rsa_sign.c \
	rsa_sp800_56b_check.c \
	rsa_sp800_56b_gen.c \
	rsa_x931.c \
	rsa_x931g.c

CRYPTO_SRC_SHA = \
	sha1_one.c \
	sha1dgst.c \
	sha256.c \
	sha3.c \
	sha512.c \
	keccak1600.c

CRYPTO_SRC_EC = \
	curve25519.c \
	curve448/arch_32/f_impl32.c \
	curve448/arch_64/f_impl64.c \
	curve448/curve448.c \
	curve448/curve448_tables.c \
	curve448/eddsa.c \
	curve448/f_generic.c \
	curve448/scalar.c \
	ec2_oct.c \
	ec2_smpl.c \
	ec_ameth.c \
	ec_asn1.c \
	ec_backend.c \
	ec_check.c \
	ec_curve.c \
	ec_cvt.c \
	ec_deprecated.c \
	ec_err.c \
	ec_key.c \
	ec_kmeth.c \
	ec_lib.c \
	ec_mult.c \
	ec_oct.c \
	ec_pmeth.c \
	ec_print.c \
	ecdh_kdf.c \
	ecdh_ossl.c \
	ecdsa_ossl.c \
	ecdsa_sign.c \
	ecdsa_vrf.c \
	eck_prn.c \
	ecp_mont.c \
	ecp_nist.c \
	ecp_oct.c \
	ecp_smpl.c \
	ecx_backend.c \
	ecx_key.c \
	ecx_meth.c
#	ecp_nistz256.c \

CRYPTO_SRC_EVP = \
	asymcipher.c \
	bio_b64.c \
	bio_enc.c \
	bio_md.c \
	bio_ok.c \
	c_allc.c \
	c_alld.c \
	cmeth_lib.c \
	ctrl_params_translate.c \
	dh_ctrl.c \
	dh_support.c \
	digest.c \
	dsa_ctrl.c \
	e_aes.c \
	e_aes_cbc_hmac_sha1.c \
	e_aes_cbc_hmac_sha256.c \
	e_aria.c \
	e_bf.c \
	e_camellia.c \
	e_cast.c \
	e_chacha20_poly1305.c \
	e_des.c \
	e_des3.c \
	e_idea.c \
	e_null.c \
	e_old.c \
	e_rc2.c \
	e_rc4.c \
	e_rc4_hmac_md5.c \
	e_rc5.c \
	e_seed.c \
	e_sm4.c \
	e_xcbc_d.c \
	ec_ctrl.c \
	ec_support.c \
	encode.c \
	evp_cnf.c \
	evp_enc.c \
	evp_err.c \
	evp_fetch.c \
	evp_key.c \
	evp_lib.c \
	evp_pbe.c \
	evp_pkey.c \
	evp_rand.c \
	evp_utils.c \
	exchange.c \
	kdf_lib.c \
	kdf_meth.c \
	kem.c \
	keymgmt_lib.c \
	keymgmt_meth.c \
	legacy_blake2.c \
	legacy_md2.c \
	legacy_md5.c \
	legacy_md5_sha1.c \
	legacy_mdc2.c \
	legacy_sha.c \
	m_null.c \
	m_sigver.c \
	mac_lib.c \
	mac_meth.c \
	names.c \
	p5_crpt.c \
	p5_crpt2.c \
	p_dec.c \
	p_enc.c \
	p_legacy.c \
	p_lib.c \
	p_open.c \
	p_seal.c \
	p_sign.c \
	p_verify.c \
	pbe_scrypt.c \
	pmeth_check.c \
	pmeth_gn.c \
	pmeth_lib.c \
	signature.c

CRYPTO_SRC_ROOT = \
	asn1_dsa.c \
	bsearch.c \
	context.c \
	core_algorithm.c \
	core_fetch.c \
	core_namemap.c \
	cpt_err.c \
	cpuid.c \
	cryptlib.c \
	ctype.c \
	cversion.c \
	der_writer.c \
	deterministic_nonce.c \
	ebcdic.c \
	ex_data.c \
	getenv.c \
	info.c \
	init.c \
	initthread.c \
	mem.c \
	mem_sec.c \
	o_dir.c \
	o_fopen.c \
	o_init.c \
	o_str.c \
	o_time.c \
	packet.c \
	param_build.c \
	param_build_set.c \
	params.c \
	params_dup.c \
	params_from_text.c \
	passphrase.c \
	provider.c \
	provider_child.c \
	provider_conf.c \
	provider_core.c \
	provider_predefined.c \
	punycode.c \
	self_test_core.c \
	sleep.c \
	sparse_array.c \
	threads_iprt.c \
	time.c \
	trace.c \
	uid.c \
	mem_clr.c \
	quic_vlint.c

CRYPTO_SRC_ERR = \
	err.c \
	err_all.c \
	err_all_legacy.c \
	err_blocks.c \
	err_mark.c \
	err_prn.c \
	err_save.c

CRYPTO_SRC_BN = \
	bn_add.c \
	bn_blind.c \
	bn_const.c \
	bn_conv.c \
	bn_ctx.c \
	bn_depr.c \
	bn_dh.c \
	bn_div.c \
	bn_err.c \
	bn_exp.c \
	bn_exp2.c \
	bn_gcd.c \
	bn_gf2m.c \
	bn_intern.c \
	bn_kron.c \
	bn_lib.c \
	bn_mod.c \
	bn_mont.c \
	bn_mpi.c \
	bn_mul.c \
	bn_nist.c \
	bn_prime.c \
	bn_print.c \
	bn_rand.c \
	bn_recp.c \
	bn_rsa_fips186_4.c \
	bn_shift.c \
	bn_sqr.c \
	bn_sqrt.c \
	bn_srp.c \
	bn_word.c \
	bn_x931p.c \
	rsaz_exp.c \
	rsaz_exp_x2.c

ifdef VBOX_WITH_CRYPTO_ASM
else
 CRYPTO_SRC_BN += bn_asm.c
endif

CRYPTO_SRC_AES = \
	aes_cfb.c \
	aes_ecb.c \
	aes_ige.c \
	aes_misc.c \
	aes_ofb.c \
	aes_wrap.c

ifdef VBOX_WITH_CRYPTO_ASM
else
CRYPTO_SRC_AES += \
	aes_core.c \
	aes_cbc.c
endif

CRYPTO_SRC_MODES = \
	cbc128.c \
	ccm128.c \
	cfb128.c \
	ctr128.c \
	cts128.c \
	gcm128.c \
	ocb128.c \
	ofb128.c \
	siv128.c \
	wrap128.c \
	xts128.c \
	xts128gb.c

CRYPTO_SRC_CMP = \
	cmp_asn.c \
	cmp_client.c \
	cmp_ctx.c \
	cmp_err.c \
	cmp_genm.c \
	cmp_hdr.c \
	cmp_http.c \
	cmp_msg.c \
	cmp_protect.c \
	cmp_server.c \
	cmp_status.c \
	cmp_util.c \
	cmp_vfy.c

CRYPTO_SRC_BIO = \
	bf_buff.c \
	bf_lbuf.c \
	bf_nbio.c \
	bf_null.c \
	bf_prefix.c \
	bf_readbuff.c \
	bio_addr.c \
	bio_cb.c \
	bio_dump.c \
	bio_err.c \
	bio_lib.c \
	bio_meth.c \
	bio_print.c \
	bio_sock.c \
	bio_sock2.c \
	bss_acpt.c \
	bss_bio.c \
	bss_conn.c \
	bss_core.c \
	bss_dgram.c \
	bss_dgram_pair.c \
	bss_fd.c \
	bss_file.c \
	bss_log.c \
	bss_mem.c \
	bss_null.c \
	bss_sock.c \
	ossl_core_bio.c

CRYPTO_SRC_RAND = \
	prov_seed.c \
	rand_deprecated.c \
	rand_egd.c \
	rand_err.c \
	rand_lib.c \
	rand_meth.c \
	rand_pool.c \
	rand_uniform.c \
	randfile.c

CRYPTO_SRC_BUFFER = \
	buf_err.c \
	buffer.c

CRYPTO_SRC_ENCODE_DECODE = \
	decoder_err.c \
	decoder_lib.c \
	decoder_meth.c \
	decoder_pkey.c \
	encoder_err.c \
	encoder_lib.c \
	encoder_meth.c \
	encoder_pkey.c

CRYPTO_SRC_PROPERTY = \
	defn_cache.c \
	property.c \
	property_err.c \
	property_parse.c \
	property_query.c \
	property_string.c

CRYPTO_SRC_CONF = \
	conf_api.c \
	conf_def.c \
	conf_err.c \
	conf_lib.c \
	conf_mall.c \
	conf_mod.c \
	conf_sap.c \
	conf_ssl.c

CRYPTO_SRC_LHASH = \
	lh_stats.c \
	lhash.c

CRYPTO_SRC_ASN1 = \
	a_bitstr.c \
	a_d2i_fp.c \
	a_digest.c \
	a_dup.c \
	a_gentm.c \
	a_i2d_fp.c \
	a_int.c \
	a_mbstr.c \
	a_object.c \
	a_octet.c \
	a_print.c \
	a_sign.c \
	a_strex.c \
	a_strnid.c \
	a_time.c \
	a_type.c \
	a_utctm.c \
	a_utf8.c \
	a_verify.c \
	ameth_lib.c \
	asn1_err.c \
	asn1_gen.c \
	asn1_item_list.c \
	asn1_lib.c \
	asn1_parse.c \
	asn_mime.c \
	asn_moid.c \
	asn_mstbl.c \
	asn_pack.c \
	bio_asn1.c \
	bio_ndef.c \
	d2i_param.c \
	d2i_pr.c \
	d2i_pu.c \
	evp_asn1.c \
	f_int.c \
	f_string.c \
	i2d_evp.c \
	n_pkey.c \
	nsseq.c \
	p5_pbe.c \
	p5_pbev2.c \
	p5_scrypt.c \
	p8_pkey.c \
	t_bitst.c \
	t_pkey.c \
	t_spki.c \
	tasn_dec.c \
	tasn_enc.c \
	tasn_fre.c \
	tasn_new.c \
	tasn_prn.c \
	tasn_scn.c \
	tasn_typ.c \
	tasn_utl.c \
	x_algor.c \
	x_bignum.c \
	x_info.c \
	x_int64.c \
	x_long.c \
	x_pkey.c \
	x_sig.c \
	x_spki.c \
	x_val.c

CRYPTO_SRC_ENGINE = \
	eng_all.c \
	eng_cnf.c \
	eng_ctrl.c \
	eng_dyn.c \
	eng_err.c \
	eng_fat.c \
	eng_init.c \
	eng_lib.c \
	eng_list.c \
	eng_openssl.c \
	eng_pkey.c \
	eng_rdrand.c \
	eng_table.c \
	tb_asnmth.c \
	tb_cipher.c \
	tb_dh.c \
	tb_digest.c \
	tb_dsa.c \
	tb_eckey.c \
	tb_pkmeth.c \
	tb_rand.c \
	tb_rsa.c

CRYPTO_SRC_OBJECTS = \
	o_names.c \
	obj_dat.c \
	obj_err.c \
	obj_lib.c \
	obj_xref.c

CRYPTO_SRC_X509 = \
	by_dir.c \
	by_file.c \
	by_store.c \
	pcy_cache.c \
	pcy_data.c \
	pcy_lib.c \
	pcy_map.c \
	pcy_node.c \
	pcy_tree.c \
	t_crl.c \
	t_req.c \
	t_x509.c \
	v3_addr.c \
	v3_admis.c \
	v3_akeya.c \
	v3_akid.c \
	v3_asid.c \
	v3_bcons.c \
	v3_bitst.c \
	v3_conf.c \
	v3_cpols.c \
	v3_crld.c \
	v3_enum.c \
	v3_extku.c \
	v3_genn.c \
	v3_group_ac.c \
	v3_ia5.c \
	v3_ind_iss.c \
	v3_info.c \
	v3_int.c \
	v3_ist.c \
	v3_lib.c \
	v3_ncons.c \
	v3_no_ass.c \
	v3_no_rev_avail.c \
	v3_pci.c \
	v3_pcia.c \
	v3_pcons.c \
	v3_pku.c \
	v3_pmaps.c \
	v3_prn.c \
	v3_purp.c \
	v3_san.c \
	v3_single_use.c \
	v3_skid.c \
	v3_soa_id.c \
	v3_sxnet.c \
	v3_tlsf.c \
	v3_utf8.c \
	v3_utl.c \
	v3err.c \
	x509_att.c \
	x509_cmp.c \
	x509_d2.c \
	x509_def.c \
	x509_err.c \
	x509_ext.c \
	x509_lu.c \
	x509_meth.c \
	x509_obj.c \
	x509_r2x.c \
	x509_req.c \
	x509_set.c \
	x509_trust.c \
	x509_txt.c \
	x509_v3.c \
	x509_vfy.c \
	x509_vpm.c \
	x509cset.c \
	x509name.c \
	x509rset.c \
	x509spki.c \
	x509type.c \
	x_all.c \
	x_attrib.c \
	x_crl.c \
	x_exten.c \
	x_name.c \
	x_pubkey.c \
	x_req.c \
	x_x509.c \
	x_x509a.c

CRYPTO_SRC_DH = \
	dh_ameth.c \
	dh_asn1.c \
	dh_backend.c \
	dh_check.c \
	dh_depr.c \
	dh_err.c \
	dh_gen.c \
	dh_group_params.c \
	dh_kdf.c \
	dh_key.c \
	dh_lib.c \
	dh_meth.c \
	dh_pmeth.c \
	dh_prn.c \
	dh_rfc5114.c

CRYPTO_SRC_DSA = \
	dsa_ameth.c \
	dsa_asn1.c \
	dsa_backend.c \
	dsa_check.c \
	dsa_depr.c \
	dsa_err.c \
	dsa_gen.c \
	dsa_key.c \
	dsa_lib.c \
	dsa_meth.c \
	dsa_ossl.c \
	dsa_pmeth.c \
	dsa_prn.c \
	dsa_sign.c \
	dsa_vrf.c

CRYPTO_SRC_CMS = \
	cms_asn1.c \
	cms_att.c \
	cms_cd.c \
	cms_dd.c \
	cms_dh.c \
	cms_ec.c \
	cms_enc.c \
	cms_env.c \
	cms_err.c \
	cms_ess.c \
	cms_io.c \
	cms_kari.c \
	cms_lib.c \
	cms_pwri.c \
	cms_rsa.c \
	cms_sd.c \
	cms_smime.c

CRYPTO_SRC_OCSP = \
	ocsp_asn.c \
	ocsp_cl.c \
	ocsp_err.c \
	ocsp_ext.c \
	ocsp_http.c \
	ocsp_lib.c \
	ocsp_prn.c \
	ocsp_srv.c \
	ocsp_vfy.c \
	v3_ocsp.c

CRYPTO_SRC_PKCS12 = \
	p12_add.c \
	p12_asn.c \
	p12_attr.c \
	p12_crpt.c \
	p12_crt.c \
	p12_decr.c \
	p12_init.c \
	p12_key.c \
	p12_kiss.c \
	p12_mutl.c \
	p12_npas.c \
	p12_p8d.c \
	p12_p8e.c \
	p12_sbag.c \
	p12_utl.c \
	pk12err.c

CRYPTO_SRC_PKCS7 = \
	bio_pk7.c \
	pk7_asn1.c \
	pk7_attr.c \
	pk7_doit.c \
	pk7_lib.c \
	pk7_mime.c \
	pk7_smime.c \
	pkcs7err.c

CRYPTO_SRC_PEM = \
	pem_all.c \
	pem_err.c \
	pem_info.c \
	pem_lib.c \
	pem_oth.c \
	pem_pk8.c \
	pem_pkey.c \
	pem_sign.c \
	pem_x509.c \
	pem_xaux.c \
	pvkfmt.c

CRYPTO_SRC_ESS = \
	ess_asn1.c \
	ess_err.c \
	ess_lib.c

CRYPTO_SRC_FFC = \
	ffc_backend.c \
	ffc_dh.c \
	ffc_key_generate.c \
	ffc_key_validate.c \
	ffc_params.c \
	ffc_params_generate.c \
	ffc_params_validate.c

CRYPTO_SRC_STORE = \
	store_err.c \
	store_init.c \
	store_lib.c \
	store_meth.c \
	store_register.c \
	store_result.c \
	store_strings.c

CRYPTO_SRC_CAMELLIA = \
	camellia.c \
	cmll_cbc.c \
	cmll_cfb.c \
	cmll_ctr.c \
	cmll_ecb.c \
	cmll_misc.c \
	cmll_ofb.c

CRYPTO_SRC_CT = \
	ct_b64.c \
	ct_err.c \
	ct_log.c \
	ct_oct.c \
	ct_policy.c \
	ct_prn.c \
	ct_sct.c \
	ct_sct_ctx.c \
	ct_vfy.c \
	ct_x509v3.c

CRYPTO_SRC_UI = \
	ui_err.c \
	ui_lib.c \
	ui_null.c \
	ui_openssl.c \
	ui_util.c

CRYPTO_SRC_HTTP = \
	http_client.c \
	http_err.c \
	http_lib.c

CRYPTO_SRC_HPKE = \
	hpke.c \
	hpke_util.c

CRYPTO_SRC_DES = \
	cbc_cksm.c \
	cbc_enc.c \
	cfb64ede.c \
	cfb64enc.c \
	cfb_enc.c \
	des_enc.c \
	ecb3_enc.c \
	ecb_enc.c \
	fcrypt.c \
	fcrypt_b.c \
	ofb64ede.c \
	ofb64enc.c \
	ofb_enc.c \
	pcbc_enc.c \
	qud_cksm.c \
	rand_key.c \
	set_key.c \
	str2key.c \
	xcbc_enc.c

CRYPTO_SRC_DSO = \
	dso_dl.c \
	dso_dlfcn.c \
	dso_err.c \
	dso_lib.c \
	dso_openssl.c \
	dso_vms.c \
	dso_win32.c

CRYPTO_SRC_SEED = \
	seed.c \
	seed_cbc.c \
	seed_cfb.c \
	seed_ecb.c \
	seed_ofb.c

CRYPTO_SRC_MD5 = \
	md5_dgst.c \
	md5_one.c \
	md5_sha1.c

CRYPTO_SRC_MD2 = \
	md2_dgst.c \
	md2_one.c

CRYPTO_SRC_ARIA = \
	aria.c

CRYPTO_SRC_CHACHA = \
	chacha_enc.c

CRYPTO_SRC_RC4 = \
	rc4_enc.c \
	rc4_skey.c

CRYPTO_SRC_MDC2 = \
	mdc2_one.c \
	mdc2dgst.c

CRYPTO_SRC_POLY1305 = \
	poly1305.c

CRYPTO_SRC_SM3 = \
	legacy_sm3.c \
	sm3.c

CRYPTO_SRC_BF = \
	bf_cfb64.c \
	bf_ecb.c \
	bf_enc.c \
	bf_ofb64.c \
	bf_skey.c

CRYPTO_SRC_ASYNC = \
	async_err.c

#CRYPTO_SRC_ASYNC = \
	arch/async_null.c \
	arch/async_posix.c \
	arch/async_win.c \
	async.c \
	async_err.c \
	async_wait.c

CRYPTO_SRC_CMAC = \
	cmac.c

CRYPTO_SRC_SIPHASH = \
	siphash.c

CRYPTO_SRC_COMP = \
	c_zlib.c \
	comp_err.c \
	comp_lib.c

#	c_brotli.c \
#	c_zstd.c \

CRYPTO_SRC_SRP = \
	srp_lib.c \
	srp_vfy.c

CRYPTO_SRC_CRMF = \
	crmf_asn.c \
	crmf_err.c \
	crmf_lib.c \
	crmf_pbm.c

CRYPTO_SRC_TS = \
	ts_asn1.c \
	ts_conf.c \
	ts_err.c \
	ts_lib.c \
	ts_req_print.c \
	ts_req_utils.c \
	ts_rsp_print.c \
	ts_rsp_sign.c \
	ts_rsp_utils.c \
	ts_rsp_verify.c \
	ts_verify_ctx.c

SRC_PROVIDERS = \
	prov_running.c \
	baseprov.c \
	defltprov.c \
	nullprov.c
#	legacyprov.c

PROVIDERS_KEM = \
	ec_kem.c \
	ecx_kem.c \
	kem_util.c \
	rsa_kem.c

PROVIDERS_COMMON = \
	bio_prov.c \
	capabilities.c \
	digest_to_nid.c \
	provider_ctx.c \
	provider_err.c \
	provider_seeding.c \
	provider_util.c \
	securitycheck.c \
	securitycheck_default.c

PROVIDERS_RANDS = \
	crngt.c \
	drbg.c \
	drbg_ctr.c \
	drbg_hash.c \
	drbg_hmac.c \
	seed_src.c \
	test_rng.c \
	seeding/rand_cpu_x86.c \
	seeding/rand_unix.c

PROVIDERS_DIGESTS = \
	blake2_prov.c \
	blake2b_prov.c \
	blake2s_prov.c \
	digestcommon.c \
	md2_prov.c \
	md5_prov.c \
	md5_sha1_prov.c \
	mdc2_prov.c \
	null_prov.c \
	sha2_prov.c \
	sha3_prov.c \
	sm3_prov.c

PROVIDERS_CIPHERS = \
	cipher_aes.c \
	cipher_aes_cbc_hmac_sha.c \
	cipher_aes_cbc_hmac_sha1_hw.c \
	cipher_aes_cbc_hmac_sha256_hw.c \
	cipher_aes_ccm.c \
	cipher_aes_ccm_hw.c \
	cipher_aes_gcm.c \
	cipher_aes_gcm_hw.c \
	cipher_aes_gcm_siv.c \
	cipher_aes_gcm_siv_hw.c \
	cipher_aes_gcm_siv_polyval.c \
	cipher_aes_hw.c \
	cipher_aes_ocb.c \
	cipher_aes_ocb_hw.c \
	cipher_aes_siv.c \
	cipher_aes_siv_hw.c \
	cipher_aes_wrp.c \
	cipher_aes_xts.c \
	cipher_aes_xts_fips.c \
	cipher_aes_xts_hw.c \
	cipher_aria.c \
	cipher_aria_ccm.c \
	cipher_aria_ccm_hw.c \
	cipher_aria_gcm.c \
	cipher_aria_gcm_hw.c \
	cipher_aria_hw.c \
	cipher_blowfish.c \
	cipher_blowfish_hw.c \
	cipher_camellia.c \
	cipher_camellia_hw.c \
	cipher_cast5.c \
	cipher_cast5_hw.c \
	cipher_chacha20.c \
	cipher_chacha20_hw.c \
	cipher_chacha20_poly1305.c \
	cipher_chacha20_poly1305_hw.c \
	cipher_cts.c \
	cipher_des.c \
	cipher_des_hw.c \
	cipher_desx.c \
	cipher_desx_hw.c \
	cipher_null.c \
	cipher_rc4.c \
	cipher_rc4_hmac_md5.c \
	cipher_rc4_hmac_md5_hw.c \
	cipher_rc4_hw.c \
	cipher_seed.c \
	cipher_seed_hw.c \
	cipher_tdes.c \
	cipher_tdes_common.c \
	cipher_tdes_default.c \
	cipher_tdes_default_hw.c \
	cipher_tdes_hw.c \
	cipher_tdes_wrap.c \
	cipher_tdes_wrap_hw.c \
	ciphercommon.c \
	ciphercommon_block.c \
	ciphercommon_ccm.c \
	ciphercommon_ccm_hw.c \
	ciphercommon_gcm.c \
	ciphercommon_gcm_hw.c \
	ciphercommon_hw.c

PROVIDERS_KMGMT = \
	dh_kmgmt.c \
	dsa_kmgmt.c \
	ec_kmgmt.c \
	ecx_kmgmt.c \
	kdf_legacy_kmgmt.c \
	mac_legacy_kmgmt.c \
	rsa_kmgmt.c

PROVIDERS_EXCHANGE = \
	dh_exch.c \
	ecdh_exch.c \
	ecx_exch.c \
	kdf_exch.c

PROVIDERS_ENCODE_DECODE = \
	decode_der2key.c \
	decode_epki2pki.c \
	decode_msblob2key.c \
	decode_pem2der.c \
	decode_pvk2key.c \
	decode_spki2typespki.c \
	encode_key2any.c \
	encode_key2blob.c \
	encode_key2ms.c \
	encode_key2text.c \
	endecoder_common.c

PROVIDERS_MACS = \
	blake2b_mac.c \
	blake2s_mac.c \
	cmac_prov.c \
	gmac_prov.c \
	hmac_prov.c \
	kmac_prov.c \
	poly1305_prov.c \
	siphash_prov.c

PROVIDERS_KDFS = \
	argon2.c \
	hmacdrbg_kdf.c \
	hkdf.c \
	kbkdf.c \
	krb5kdf.c \
	pbkdf1.c \
	pbkdf2.c \
	pbkdf2_fips.c \
	pkcs12kdf.c \
	pvkkdf.c \
	scrypt.c \
	sshkdf.c \
	sskdf.c \
	tls1_prf.c \
	x942kdf.c

PROVIDERS_SIGNATURE = \
	dsa_sig.c \
	ecdsa_sig.c \
	eddsa_sig.c \
	mac_legacy_sig.c \
	rsa_sig.c

PROVIDERS_COMMON_DER = \
	der_dsa_key.c \
	der_dsa_sig.c \
	der_ec_key.c \
	der_ec_sig.c \
	der_ecx_key.c \
	der_rsa_key.c \
	der_rsa_sig.c \
	der_sm2_key.c \
	der_sm2_sig.c

GEN_SOURCES_DER = \
	der_digests_gen.c \
	der_dsa_gen.c \
	der_ec_gen.c \
	der_ecx_gen.c \
	der_rsa_gen.c \
	der_sm2_gen.c \
	der_wrap_gen.c

SRC_SSL = \
	bio_ssl.c \
	d1_lib.c \
	d1_msg.c \
	d1_srtp.c \
	methods.c \
	pqueue.c \
	priority_queue.c \
	record/rec_layer_d1.c \
	record/rec_layer_s3.c \
	record/methods/dtls_meth.c \
	record/methods/ssl3_cbc.c \
	record/methods/ssl3_meth.c \
	record/methods/tls1_meth.c \
	record/methods/tls13_meth.c \
	record/methods/tlsany_meth.c \
	record/methods/tls_common.c \
	record/methods/tls_multib.c \
	record/methods/tls_pad.c \
	s3_enc.c \
	s3_lib.c \
	s3_msg.c \
	ssl_asn1.c \
	ssl_cert.c \
	ssl_cert_comp.c \
	ssl_ciph.c \
	ssl_conf.c \
	ssl_err.c \
	ssl_init.c \
	ssl_lib.c \
	ssl_mcnf.c \
	ssl_rsa.c \
	ssl_sess.c \
	ssl_stat.c \
	ssl_txt.c \
	ssl_utst.c \
	statem/extensions.c \
	statem/extensions_clnt.c \
	statem/extensions_cust.c \
	statem/extensions_srvr.c \
	statem/statem.c \
	statem/statem_clnt.c \
	statem/statem_dtls.c \
	statem/statem_lib.c \
	statem/statem_srvr.c \
	t1_enc.c \
	t1_lib.c \
	t1_trce.c \
	tls13_enc.c \
	tls_depr.c \
	tls_srp.c

SRC_SSL_QUIC = \
	quic_impl.c \
	quic_method.c \
	quic_wire.c quic_ackm.c quic_statm.c \
	cc_newreno.c quic_demux.c quic_record_rx.c \
	quic_record_tx.c quic_record_util.c quic_record_shared.c quic_wire_pkt.c \
	quic_rx_depack.c \
	quic_fc.c uint_set.c \
	quic_cfq.c quic_txpim.c quic_fifd.c quic_txp.c \
	quic_stream_map.c \
	quic_sf_list.c quic_rstream.c quic_sstream.c \
	quic_reactor.c \
	quic_channel.c quic_port.c quic_engine.c \
	quic_tserver.c \
	quic_tls.c \
	quic_thread_assist.c \
	quic_trace.c \
	quic_srtm.c quic_srt_gen.c \
	quic_lcidm.c quic_rcidm.c \
	quic_types.c \
	qlog_event_helpers.c \
	qlog.c json_enc.c

SRC_S += crypto/genasm-elf/ecp_nistz256-x86_64.S

SRC_C += gen-sources/crypto/params_idx.c
SRC_C += $(addprefix gen-sources/der/, $(GEN_SOURCES_DER))

SRC_C += crypto/hmac/hmac.c

SRC_C += crypto/txt_db/txt_db.c

SRC_C += crypto/stack/stack.c

SRC_C += crypto/thread/api.c
SRC_C += crypto/thread/arch.c
SRC_C += crypto/thread/internal.c
SRC_C += crypto/thread/arch/thread_posix.c

SRC_C += $(addprefix ssl/,      $(SRC_SSL))
SRC_C += $(addprefix ssl/quic/, $(SRC_SSL_QUIC))

INC_DIR += $(VIRTUALBOX_DIR)/src/VBox/Devices/EFI/Firmware/CryptoPkg/Library/OpensslLib

# required by CRYPTO_SRC_COMP
#INC_DIR += $(VIRTUALBOX_DIR)/src/VBox/Devices/EFI/Firmware/BaseTools/Source/C/BrotliCompress/include

SRC_C += $(addprefix crypto/,               $(CRYPTO_SRC_ROOT))
SRC_C += $(addprefix crypto/aes/,           $(CRYPTO_SRC_AES))
SRC_C += $(addprefix crypto/asn1/,          $(CRYPTO_SRC_ASN1))
SRC_C += $(addprefix crypto/aria/,          $(CRYPTO_SRC_ARIA))
SRC_C += $(addprefix crypto/async/,         $(CRYPTO_SRC_ASYNC))
SRC_C += $(addprefix crypto/bf/,            $(CRYPTO_SRC_BF))
SRC_C += $(addprefix crypto/bio/,           $(CRYPTO_SRC_BIO))
SRC_C += $(addprefix crypto/bn/,            $(CRYPTO_SRC_BN))
SRC_C += $(addprefix crypto/buffer/,        $(CRYPTO_SRC_BUFFER))
SRC_C += $(addprefix crypto/cast/,          $(CRYPTO_SRC_CAST))
SRC_C += $(addprefix crypto/camellia/,      $(CRYPTO_SRC_CAMELLIA))
SRC_C += $(addprefix crypto/chacha/,        $(CRYPTO_SRC_CHACHA))
SRC_C += $(addprefix crypto/cmac/,          $(CRYPTO_SRC_CMAC))
SRC_C += $(addprefix crypto/cmp/,           $(CRYPTO_SRC_CMP))
SRC_C += $(addprefix crypto/cms/,           $(CRYPTO_SRC_CMS))
SRC_C += $(addprefix crypto/conf/,          $(CRYPTO_SRC_CONF))
SRC_C += $(addprefix crypto/comp/,          $(CRYPTO_SRC_COMP))
SRC_C += $(addprefix crypto/crmf/,          $(CRYPTO_SRC_CRMF))
SRC_C += $(addprefix crypto/ct/,            $(CRYPTO_SRC_CT))
SRC_C += $(addprefix crypto/dh/,            $(CRYPTO_SRC_DH))
SRC_C += $(addprefix crypto/des/,           $(CRYPTO_SRC_DES))
SRC_C += $(addprefix crypto/dsa/,           $(CRYPTO_SRC_DSA))
SRC_C += $(addprefix crypto/dso/,           $(CRYPTO_SRC_DSO))
SRC_C += $(addprefix crypto/ec/,            $(CRYPTO_SRC_EC))
SRC_C += $(addprefix crypto/encode_decode/, $(CRYPTO_SRC_ENCODE_DECODE))
SRC_C += $(addprefix crypto/engine/,        $(CRYPTO_SRC_ENGINE))
SRC_C += $(addprefix crypto/err/,           $(CRYPTO_SRC_ERR))
SRC_C += $(addprefix crypto/ess/,           $(CRYPTO_SRC_ESS))
SRC_C += $(addprefix crypto/evp/,           $(CRYPTO_SRC_EVP))
SRC_C += $(addprefix crypto/ffc/,           $(CRYPTO_SRC_FFC))
SRC_C += $(addprefix crypto/http/,          $(CRYPTO_SRC_HTTP))
SRC_C += $(addprefix crypto/hpke/,          $(CRYPTO_SRC_HPKE))
SRC_C += $(addprefix crypto/lhash/,         $(CRYPTO_SRC_LHASH))
SRC_C += $(addprefix crypto/md2/,           $(CRYPTO_SRC_MD2))
SRC_C += $(addprefix crypto/md5/,           $(CRYPTO_SRC_MD5))
SRC_C += $(addprefix crypto/mdc2/,          $(CRYPTO_SRC_MDC2))
SRC_C += $(addprefix crypto/modes/,         $(CRYPTO_SRC_MODES))
SRC_C += $(addprefix crypto/objects/,       $(CRYPTO_SRC_OBJECTS))
SRC_C += $(addprefix crypto/ocsp/,          $(CRYPTO_SRC_OCSP))
SRC_C += $(addprefix crypto/pem/,           $(CRYPTO_SRC_PEM))
SRC_C += $(addprefix crypto/poly1305/,      $(CRYPTO_SRC_POLY1305))
SRC_C += $(addprefix crypto/pkcs7/,         $(CRYPTO_SRC_PKCS7))
SRC_C += $(addprefix crypto/pkcs12/,        $(CRYPTO_SRC_PKCS12))
SRC_C += $(addprefix crypto/property/,      $(CRYPTO_SRC_PROPERTY))
SRC_C += $(addprefix crypto/rand/,          $(CRYPTO_SRC_RAND))
SRC_C += $(addprefix crypto/rc4/,           $(CRYPTO_SRC_RC4))
SRC_C += $(addprefix crypto/rsa/,           $(CRYPTO_SRC_RSA))
SRC_C += $(addprefix crypto/seed/,          $(CRYPTO_SRC_SEED))
SRC_C += $(addprefix crypto/sha/,           $(CRYPTO_SRC_SHA))
SRC_C += $(addprefix crypto/siphash/,       $(CRYPTO_SRC_SIPHASH))
SRC_C += $(addprefix crypto/sm3/,           $(CRYPTO_SRC_SM3))
SRC_C += $(addprefix crypto/srp/,           $(CRYPTO_SRC_SRP))
SRC_C += $(addprefix crypto/store/,         $(CRYPTO_SRC_STORE))
SRC_C += $(addprefix crypto/ts/,            $(CRYPTO_SRC_TS))
SRC_C += $(addprefix crypto/ui/,            $(CRYPTO_SRC_UI))
SRC_C += $(addprefix crypto/x509/,          $(CRYPTO_SRC_X509))

SRC_C += $(addprefix providers/, $(SRC_PROVIDERS))
SRC_C += $(addprefix providers/common/, $(PROVIDERS_COMMON))
SRC_C += $(addprefix providers/common/der/, $(PROVIDERS_COMMON_DER))
SRC_C += $(addprefix providers/implementations/ciphers/,       $(PROVIDERS_CIPHERS))
SRC_C += $(addprefix providers/implementations/digests/,       $(PROVIDERS_DIGESTS))
SRC_C += $(addprefix providers/implementations/encode_decode/, $(PROVIDERS_ENCODE_DECODE))
SRC_C += $(addprefix providers/implementations/exchange/,      $(PROVIDERS_EXCHANGE))
SRC_C += $(addprefix providers/implementations/kdfs/,          $(PROVIDERS_KDFS))
SRC_C += $(addprefix providers/implementations/kem/,           $(PROVIDERS_KEM))
SRC_C += $(addprefix providers/implementations/keymgmt/,       $(PROVIDERS_KMGMT))
SRC_C += $(addprefix providers/implementations/macs/,          $(PROVIDERS_MACS))
SRC_C += $(addprefix providers/implementations/rands/,         $(PROVIDERS_RANDS))
SRC_C += $(addprefix providers/implementations/signature/,     $(PROVIDERS_SIGNATURE))

SRC_C += providers/implementations/storemgmt/file_store.c
SRC_C += providers/implementations/storemgmt/file_store_any2obj.c
SRC_C += providers/implementations/asymciphers/rsa_enc.c

VBOX_CC_OPT += -DOPENSSLDIR=\"\"
VBOX_CC_OPT += -DOPENSSL_NO_WINSTORE
VBOX_CC_OPT += -DDSO_DLFCN -DHAVE_DLFCN_H

vpath %.c $(OPENSSL_DIR)
vpath %.S $(OPENSSL_DIR)

#CC_CXX_WARN_STRICT =
