#include <base/log.h>

extern "C" {
#include <asm/mach-imx/sci/rpc.h>
}

template <typename T>
static T read_mu(sc_ipc_t const ipc, unsigned const offset)
{
	return *reinterpret_cast<T *>(ipc + offset);
}

#if 0
template <typename T>
static write_mu(sc_ipc_t const ipc, unsigned const offset)
{
	*reinterpret_cast<T *>(ipc + offset)
}
#endif


#define __IO
#define __I

typedef struct {
  __IO uint32_t TR[4];                             /**< Transmit Register n, array offset: 0x20, array step: 0x4 */
  __I  uint32_t RR[4];                             /**< Receive Register n, array offset: 0x40, array step: 0x4 */
  __IO uint32_t SR;                                /**< Status Register, offset: 0x60 */
  __IO uint32_t CR;                                /**< Control Register, offset: 0x64 */
} MU_Type;

#define MU_TR_REG(base, index)       ((base)->TR[index])
#define MU_WR_TR(base, index, value) (MU_TR_REG(base, index) = (value))
#define MU_SR_REG(base)              ((base)->SR)
#define MU_RD_SR(base)               (MU_SR_REG(base))
#define MU_RR_REG(base,index)        ((base)->RR[index])
#define MU_RD_RR(base, index)        (MU_RR_REG(base, index))


extern "C"
void sc_call_rpc(sc_ipc_t ipc, sc_rpc_msg_t *msg, sc_bool_t no_wait)
{
	sc_ipc_write(ipc, msg);
	if (!no_wait)
		sc_ipc_read(ipc, msg);
}

void MU_HAL_SendMsg(MU_Type *base, uint32_t regIndex, uint32_t msg)
{
	#define MU_SR_TE0_MASK		(1 << 23)
	#define MU_TR_COUNT         4

	if (regIndex >= MU_TR_COUNT) {
		Genode::error(__func__, " invalid index");
		return;
	}

	uint32_t mask = MU_SR_TE0_MASK >> regIndex;
	/* Wait TX register to be empty. */
	while (!(MU_RD_SR(base) & mask)) { }
	MU_WR_TR(base, regIndex, msg);
}

void MU_HAL_ReceiveMsg(MU_Type *base, uint32_t regIndex, uint32_t *msg)
{
	if (regIndex >= MU_TR_COUNT) {
		Genode::error(__func__, " invalid index");
		return;
	}

	#define MU_SR_RF0_MASK (1 << 27)
	#define MU_RR_COUNT                              4

	uint32_t mask = MU_SR_RF0_MASK >> regIndex;

	/* Wait RX register to be full. */
	while (!(MU_RD_SR(base) & mask)) { }
	*msg = MU_RD_RR(base, regIndex);
}

extern "C"
void sc_ipc_write(sc_ipc_t const ipc, void const * const data)
{
	sc_rpc_msg_t *msg = (sc_rpc_msg_t *) data;
	if (!msg) {
		Genode::error(__func__, " msg is null");
		return;
	}

	if (msg->size > SC_RPC_MAX_MSG) {
		Genode::error(__func__, " size too large");
		return;
	}

    /* Write first word */
	unsigned count = 0;
    MU_Type *base = (MU_Type*) ipc;

    MU_HAL_SendMsg(base, 0, *((uint32_t*) msg));
    count++;

    /* Write remaining words */
    while (count < msg->size)
    {
        MU_HAL_SendMsg(base, count % MU_TR_COUNT,
            msg->DATA.u32[count - 1]);

        count++;
    }
}

extern "C"
void sc_ipc_read(sc_ipc_t const ipc, void * const data)
{
    MU_Type *base = (MU_Type*) ipc;
    sc_rpc_msg_t *msg = (sc_rpc_msg_t*) data;
    uint8_t count = 0;

    /* Check parms */
    if ((base == NULL) || (msg == NULL))
        return;

    /* Read first word */
    MU_HAL_ReceiveMsg(base, 0, (uint32_t*) msg);
    count++;

    /* Check size */
    if (msg->size > SC_RPC_MAX_MSG)
    {
        *((uint32_t*) msg) = 0;
        return;
    }

	Genode::log(__func__, " ", msg->size);

    /* Read remaining words */
    while (count < msg->size)
    {
        MU_HAL_ReceiveMsg(base, count % MU_RR_COUNT,
            &(msg->DATA.u32[count - 1]));

        count++;
    }
	Genode::log(__func__);
}

/* The id is used as pointer to the mapped iomem */

extern "C"
sc_err_t sc_ipc_open(sc_ipc_t *ipc, sc_ipc_id_t id)
{
	if (!ipc)
		return SC_ERR_IPC;

	*ipc = id;

	unsigned version = read_mu<unsigned>(*ipc, 0 /* offset */) >> 16;
	Genode::log("mu version=", version);

	return SC_ERR_NONE;
}
