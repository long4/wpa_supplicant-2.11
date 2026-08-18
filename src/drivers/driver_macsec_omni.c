/*
 * Driver interface for Omni SoC MACsec Hardware Offload (omnieth)
 * Copyright (c) 2026, OMNI CORPORATION & AFFILIATES. All rights reserved.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "utils/includes.h"
#include "utils/common.h"
#include "utils/eloop.h"
#include "common/ieee802_1x_defs.h"
#include "driver.h"
#include "driver_wired_common.h"
#include "pae/ieee802_1x_kay.h"

#include "oeth_export.h"
#include "osi_macsec.h"
#include "omnieth/ether_export.h"
#include "omnieth/macsec.h"

#include <sys/socket.h>
#include <linux/netlink.h>
#include <linux/genetlink.h>

#define OMNI_MACSEC_DRV_PREFIX "MACsec Omni: "
#define OB_MACSEC_GENL_NAME "ob_macsec_genl"

struct macsec_omni_drv_data {
	struct driver_wired_common_data common;
	int nl_fd;
	int nl_family_id;
	u32 nl_seq;
	enum macsec_cap capability;
	bool macsec_init_done;
};

/* Netlink helper to resolve Generic Netlink family ID */
static int omni_macsec_get_family_id(struct macsec_omni_drv_data *drv)
{
	struct {
		struct nlmsghdr n;
		struct genlmsghdr g;
		char buf[256];
	} req;
	struct sockaddr_nl nladdr;
	struct nlattr *attr;
	int len, attrlen;
	char resp[1024];

	os_memset(&req, 0, sizeof(req));
	req.n.nlmsg_len = NLMSG_LENGTH(GENL_HDRLEN);
	req.n.nlmsg_type = GENL_ID_CTRL;
	req.n.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
	req.n.nlmsg_seq = ++drv->nl_seq;
	req.g.cmd = CTRL_CMD_GETFAMILY;
	req.g.version = 1;

	/* Append CTRL_ATTR_FAMILY_NAME */
	attr = (struct nlattr *)((char *)&req + req.n.nlmsg_len);
	attr->nla_type = CTRL_ATTR_FAMILY_NAME;
	attr->nla_len = NLA_HDRLEN + os_strlen(OB_MACSEC_GENL_NAME) + 1;
	os_strlcpy((char *)attr + NLA_HDRLEN, OB_MACSEC_GENL_NAME,
		   os_strlen(OB_MACSEC_GENL_NAME) + 1);
	req.n.nlmsg_len += NLA_ALIGN(attr->nla_len);

	os_memset(&nladdr, 0, sizeof(nladdr));
	nladdr.nl_family = AF_NETLINK;

	if (sendto(drv->nl_fd, &req, req.n.nlmsg_len, 0,
		   (struct sockaddr *)&nladdr, sizeof(nladdr)) < 0) {
		wpa_printf(MSG_ERROR, OMNI_MACSEC_DRV_PREFIX
			   "Failed to send CTRL_CMD_GETFAMILY");
		return -1;
	}

	len = recv(drv->nl_fd, resp, sizeof(resp), 0);
	if (len < 0) {
		wpa_printf(MSG_ERROR, OMNI_MACSEC_DRV_PREFIX
			   "Failed to receive CTRL_CMD_GETFAMILY response");
		return -1;
	}

	struct nlmsghdr *nlh = (struct nlmsghdr *)resp;
	if (!NLMSG_OK(nlh, (size_t)len) || nlh->nlmsg_type == NLMSG_ERROR)
		return -1;

	struct genlmsghdr *ghdr = NLMSG_DATA(nlh);
	attr = (struct nlattr *)((char *)ghdr + GENL_HDRLEN);
	attrlen = nlh->nlmsg_len - NLMSG_HDRLEN - GENL_HDRLEN;

	while (attrlen >= (int)sizeof(struct nlattr)) {
		if (attr->nla_type == CTRL_ATTR_FAMILY_ID) {
			drv->nl_family_id = *(u16 *)((char *)attr + NLA_HDRLEN);
			wpa_printf(MSG_DEBUG, OMNI_MACSEC_DRV_PREFIX
				   "Resolved %s family ID: %d",
				   OB_MACSEC_GENL_NAME, drv->nl_family_id);
			return 0;
		}
		int nla_aligned = NLA_ALIGN(attr->nla_len);
		attrlen -= nla_aligned;
		attr = (struct nlattr *)((char *)attr + nla_aligned);
	}

	return -1;
}

/* Helper to send Netlink command to ob_macsec_genl */
static int omni_macsec_send_cmd(struct macsec_omni_drv_data *drv, u8 cmd,
				struct nlattr *nested_sa_attr)
{
	struct {
		struct nlmsghdr n;
		struct genlmsghdr g;
		char buf[1024];
	} req;
	struct sockaddr_nl nladdr;
	struct nlattr *attr;

	if (drv->nl_fd < 0 || drv->nl_family_id <= 0)
		return -1;

	os_memset(&req, 0, sizeof(req));
	req.n.nlmsg_len = NLMSG_LENGTH(GENL_HDRLEN);
	req.n.nlmsg_type = drv->nl_family_id;
	req.n.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
	req.n.nlmsg_seq = ++drv->nl_seq;
	req.g.cmd = cmd;
	req.g.version = OB_MACSEC_GENL_VERSION;

	/* Add OB_MACSEC_ATTR_IFNAME */
	attr = (struct nlattr *)((char *)&req + req.n.nlmsg_len);
	attr->nla_type = OB_MACSEC_ATTR_IFNAME;
	attr->nla_len = NLA_HDRLEN + os_strlen(drv->common.ifname) + 1;
	os_strlcpy((char *)attr + NLA_HDRLEN, drv->common.ifname,
		   os_strlen(drv->common.ifname) + 1);
	req.n.nlmsg_len += NLA_ALIGN(attr->nla_len);

	if (nested_sa_attr) {
		attr = (struct nlattr *)((char *)&req + req.n.nlmsg_len);
		os_memcpy(attr, nested_sa_attr, nested_sa_attr->nla_len);
		req.n.nlmsg_len += NLA_ALIGN(nested_sa_attr->nla_len);
	}

	os_memset(&nladdr, 0, sizeof(nladdr));
	nladdr.nl_family = AF_NETLINK;

	if (sendto(drv->nl_fd, &req, req.n.nlmsg_len, 0,
		   (struct sockaddr *)&nladdr, sizeof(nladdr)) < 0) {
		wpa_printf(MSG_ERROR, OMNI_MACSEC_DRV_PREFIX
			   "Failed to send Netlink cmd %d", cmd);
		return -1;
	}

	return 0;
}

static void * macsec_omni_wpa_init(void *ctx, const char *ifname)
{
	struct macsec_omni_drv_data *drv;

	drv = os_zalloc(sizeof(*drv));
	if (!drv)
		return NULL;

	if (driver_wired_init_common(&drv->common, ifname, ctx) < 0) {
		os_free(drv);
		return NULL;
	}

	drv->nl_fd = -1;
	drv->capability = MACSEC_CAP_INTEG_AND_CONF;

	return drv;
}

static void macsec_omni_wpa_deinit(void *priv)
{
	struct macsec_omni_drv_data *drv = priv;

	if (drv->nl_fd >= 0) {
		close(drv->nl_fd);
		drv->nl_fd = -1;
	}

	driver_wired_deinit_common(&drv->common);
	os_free(drv);
}

static int macsec_omni_macsec_init(void *priv, struct macsec_init_params *params)
{
	struct macsec_omni_drv_data *drv = priv;
	struct sockaddr_nl local;

	wpa_printf(MSG_DEBUG, OMNI_MACSEC_DRV_PREFIX "%s", __func__);

	drv->nl_fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_GENERIC);
	if (drv->nl_fd < 0) {
		wpa_printf(MSG_ERROR, OMNI_MACSEC_DRV_PREFIX
			   "Failed to open NETLINK_GENERIC socket");
		return -1;
	}

	os_memset(&local, 0, sizeof(local));
	local.nl_family = AF_NETLINK;
	if (bind(drv->nl_fd, (struct sockaddr *)&local, sizeof(local)) < 0) {
		wpa_printf(MSG_ERROR, OMNI_MACSEC_DRV_PREFIX
			   "Failed to bind NETLINK_GENERIC socket");
		close(drv->nl_fd);
		drv->nl_fd = -1;
		return -1;
	}

	if (omni_macsec_get_family_id(drv) < 0) {
		wpa_printf(MSG_ERROR, OMNI_MACSEC_DRV_PREFIX
			   "Failed to get ob_macsec_genl family ID");
		close(drv->nl_fd);
		drv->nl_fd = -1;
		return -1;
	}

	drv->macsec_init_done = true;
	return omni_macsec_send_cmd(drv, OB_MACSEC_CMD_INIT, NULL);
}

static int macsec_omni_macsec_deinit(void *priv)
{
	struct macsec_omni_drv_data *drv = priv;

	wpa_printf(MSG_DEBUG, OMNI_MACSEC_DRV_PREFIX "%s", __func__);

	if (drv->macsec_init_done) {
		omni_macsec_send_cmd(drv, OB_MACSEC_CMD_DEINIT, NULL);
		drv->macsec_init_done = false;
	}

	if (drv->nl_fd >= 0) {
		close(drv->nl_fd);
		drv->nl_fd = -1;
	}

	return 0;
}

static int macsec_omni_get_capability(void *priv, enum macsec_cap *cap)
{
	struct macsec_omni_drv_data *drv = priv;

	*cap = drv->capability;
	return 0;
}

static int macsec_omni_enable_protect_frames(void *priv, bool enabled)
{
	struct macsec_omni_drv_data *drv = priv;
	struct {
		struct nlattr nla;
		u8 val;
	} attr;

	attr.nla.nla_type = OB_MACSEC_ATTR_PROTECT_FRAMES;
	attr.nla.nla_len = sizeof(attr);
	attr.val = enabled ? 1 : 0;

	return omni_macsec_send_cmd(drv, OB_MACSEC_CMD_SET_PROTECT_FRAMES,
				    (struct nlattr *)&attr);
}

static int macsec_omni_enable_encrypt(void *priv, bool enabled)
{
	struct macsec_omni_drv_data *drv = priv;
	struct {
		struct nlattr nla;
		u8 val;
	} attr;

	attr.nla.nla_type = OB_MACSEC_ATTR_ENCRYPT;
	attr.nla.nla_len = sizeof(attr);
	attr.val = enabled ? 1 : 0;

	return omni_macsec_send_cmd(drv, OB_MACSEC_CMD_SET_ENCRYPT,
				    (struct nlattr *)&attr);
}

static int macsec_omni_set_replay_protect(void *priv, bool enabled, u32 window)
{
	struct macsec_omni_drv_data *drv = priv;
	struct {
		struct nlattr nla_en;
		u32 val_en;
		struct nlattr nla_win;
		u32 val_win;
	} attrs;

	attrs.nla_en.nla_type = OB_MACSEC_ATTR_REPLAY_PROT_EN;
	attrs.nla_en.nla_len = sizeof(attrs.nla_en) + sizeof(attrs.val_en);
	attrs.val_en = enabled ? 1 : 0;

	attrs.nla_win.nla_type = OB_MACSEC_ATTR_REPLAY_WINDOW;
	attrs.nla_win.nla_len = sizeof(attrs.nla_win) + sizeof(attrs.val_win);
	attrs.val_win = window;

	return omni_macsec_send_cmd(drv, OB_MACSEC_CMD_SET_REPLAY_PROT,
				    (struct nlattr *)&attrs);
}

static int macsec_omni_set_offload(void *priv, u8 offload)
{
	wpa_printf(MSG_DEBUG, OMNI_MACSEC_DRV_PREFIX "%s: offload=%d",
		   __func__, offload);
	return 0;
}

static int macsec_omni_set_current_cipher_suite(void *priv, u64 cs)
{
	struct macsec_omni_drv_data *drv = priv;
	struct {
		struct nlattr nla;
		u32 val;
	} attr;

	attr.nla.nla_type = OB_MACSEC_ATTR_CIPHER_SUITE;
	attr.nla.nla_len = sizeof(attr);

	if (cs == CS_ID_GCM_AES_128)
		attr.val = OSI_MACSEC_CIPHER_AES128;
	else if (cs == CS_ID_GCM_AES_256)
		attr.val = OSI_MACSEC_CIPHER_AES256;
	else
		return -1;

	return omni_macsec_send_cmd(drv, OB_MACSEC_CMD_SET_CIPHER,
				    (struct nlattr *)&attr);
}

static int macsec_omni_enable_controlled_port(void *priv, bool enabled)
{
	return 0;
}

static int macsec_omni_get_receive_lowest_pn(void *priv, struct receive_sa *sa)
{
	return 0;
}

static int macsec_omni_set_receive_lowest_pn(void *priv, struct receive_sa *sa)
{
	return 0;
}

static int macsec_omni_get_transmit_next_pn(void *priv, struct transmit_sa *sa)
{
	struct macsec_omni_drv_data *drv = priv;
	return omni_macsec_send_cmd(drv, OB_MACSEC_CMD_GET_TX_NEXT_PN, NULL);
}

static int macsec_omni_set_transmit_next_pn(void *priv, struct transmit_sa *sa)
{
	return 0;
}

static int macsec_omni_create_receive_sc(void *priv, struct receive_sc *sc,
					  unsigned int conf_offset,
					  int validation)
{
	return 0;
}

static int macsec_omni_delete_receive_sc(void *priv, struct receive_sc *sc)
{
	return 0;
}

static int macsec_omni_create_receive_sa(void *priv, struct receive_sa *sa)
{
	struct macsec_omni_drv_data *drv = priv;
	char buf[512];
	struct nlattr *nested_sa = (struct nlattr *)buf;
	char *pos = buf + NLA_HDRLEN;

	wpa_printf(MSG_DEBUG, OMNI_MACSEC_DRV_PREFIX "%s: AN=%d", __func__, sa->an);

	/* SCI */
	struct nlattr *attr = (struct nlattr *)pos;
	attr->nla_type = OB_MACSEC_SA_ATTR_SCI;
	attr->nla_len = NLA_HDRLEN + sizeof(sa->sc->sci);
	os_memcpy(pos + NLA_HDRLEN, &sa->sc->sci, sizeof(sa->sc->sci));
	pos += NLA_ALIGN(attr->nla_len);

	/* AN */
	attr = (struct nlattr *)pos;
	attr->nla_type = OB_MACSEC_SA_ATTR_AN;
	attr->nla_len = NLA_HDRLEN + sizeof(u8);
	*(u8 *)(pos + NLA_HDRLEN) = sa->an;
	pos += NLA_ALIGN(attr->nla_len);

	/* Lowest PN */
	attr = (struct nlattr *)pos;
	attr->nla_type = OB_MACSEC_SA_ATTR_LOWEST_PN;
	attr->nla_len = NLA_HDRLEN + sizeof(u32);
	*(u32 *)(pos + NLA_HDRLEN) = sa->lowest_pn;
	pos += NLA_ALIGN(attr->nla_len);

#ifdef OBPKCS_MACSEC
	/* Wrapped Key (40 bytes) & KEK Handle */
	if (sa->pkey) {
		attr = (struct nlattr *)pos;
		attr->nla_type = OB_MACSEC_SA_PKCS_KEY_WRAP;
		attr->nla_len = NLA_HDRLEN + OB_SAK_WRAPPED_LEN;
		os_memcpy(pos + NLA_HDRLEN, sa->pkey->wrapped_key, OB_SAK_WRAPPED_LEN);
		pos += NLA_ALIGN(attr->nla_len);

		attr = (struct nlattr *)pos;
		attr->nla_type = OB_MACSEC_SA_PKCS_KEK_HANDLE;
		attr->nla_len = NLA_HDRLEN + sizeof(u64);
		*(u64 *)(pos + NLA_HDRLEN) = sa->pkey->kek_handle;
		pos += NLA_ALIGN(attr->nla_len);
	}
#else
	if (sa->pkey && sa->pkey->key) {
		attr = (struct nlattr *)pos;
		attr->nla_type = OB_MACSEC_SA_ATTR_KEY;
		attr->nla_len = NLA_HDRLEN + sa->pkey->key_len;
		os_memcpy(pos + NLA_HDRLEN, sa->pkey->key, sa->pkey->key_len);
		pos += NLA_ALIGN(attr->nla_len);
	}
#endif

	nested_sa->nla_type = OB_MACSEC_ATTR_SA_CONFIG | NLA_F_NESTED;
	nested_sa->nla_len = pos - buf;

	return omni_macsec_send_cmd(drv, OB_MACSEC_CMD_CREATE_RX_SA, nested_sa);
}

static int macsec_omni_delete_receive_sa(void *priv, struct receive_sa *sa)
{
	struct macsec_omni_drv_data *drv = priv;
	char buf[128];
	struct nlattr *nested_sa = (struct nlattr *)buf;
	char *pos = buf + NLA_HDRLEN;

	struct nlattr *attr = (struct nlattr *)pos;
	attr->nla_type = OB_MACSEC_SA_ATTR_AN;
	attr->nla_len = NLA_HDRLEN + sizeof(u8);
	*(u8 *)(pos + NLA_HDRLEN) = sa->an;
	pos += NLA_ALIGN(attr->nla_len);

	nested_sa->nla_type = OB_MACSEC_ATTR_SA_CONFIG | NLA_F_NESTED;
	nested_sa->nla_len = pos - buf;

	return omni_macsec_send_cmd(drv, OB_MACSEC_CMD_DIS_RX_SA, nested_sa);
}

static int macsec_omni_enable_receive_sa(void *priv, struct receive_sa *sa)
{
	struct macsec_omni_drv_data *drv = priv;
	char buf[128];
	struct nlattr *nested_sa = (struct nlattr *)buf;
	char *pos = buf + NLA_HDRLEN;

	struct nlattr *attr = (struct nlattr *)pos;
	attr->nla_type = OB_MACSEC_SA_ATTR_AN;
	attr->nla_len = NLA_HDRLEN + sizeof(u8);
	*(u8 *)(pos + NLA_HDRLEN) = sa->an;
	pos += NLA_ALIGN(attr->nla_len);

	nested_sa->nla_type = OB_MACSEC_ATTR_SA_CONFIG | NLA_F_NESTED;
	nested_sa->nla_len = pos - buf;

	return omni_macsec_send_cmd(drv, OB_MACSEC_CMD_EN_RX_SA, nested_sa);
}

static int macsec_omni_disable_receive_sa(void *priv, struct receive_sa *sa)
{
	return macsec_omni_delete_receive_sa(priv, sa);
}

static int macsec_omni_create_transmit_sc(void *priv, struct transmit_sc *sc,
					   unsigned int conf_offset)
{
	return 0;
}

static int macsec_omni_delete_transmit_sc(void *priv, struct transmit_sc *sc)
{
	return 0;
}

static int macsec_omni_create_transmit_sa(void *priv, struct transmit_sa *sa)
{
	struct macsec_omni_drv_data *drv = priv;
	char buf[512];
	struct nlattr *nested_sa = (struct nlattr *)buf;
	char *pos = buf + NLA_HDRLEN;

	wpa_printf(MSG_DEBUG, OMNI_MACSEC_DRV_PREFIX "%s: AN=%d", __func__, sa->an);

	/* AN */
	struct nlattr *attr = (struct nlattr *)pos;
	attr->nla_type = OB_MACSEC_SA_ATTR_AN;
	attr->nla_len = NLA_HDRLEN + sizeof(u8);
	*(u8 *)(pos + NLA_HDRLEN) = sa->an;
	pos += NLA_ALIGN(attr->nla_len);

	/* PN */
	attr = (struct nlattr *)pos;
	attr->nla_type = OB_MACSEC_SA_ATTR_PN;
	attr->nla_len = NLA_HDRLEN + sizeof(u32);
	*(u32 *)(pos + NLA_HDRLEN) = sa->next_pn;
	pos += NLA_ALIGN(attr->nla_len);

#ifdef OBPKCS_MACSEC
	/* Wrapped Key (40 bytes) & KEK Handle */
	if (sa->pkey) {
		attr = (struct nlattr *)pos;
		attr->nla_type = OB_MACSEC_SA_PKCS_KEY_WRAP;
		attr->nla_len = NLA_HDRLEN + OB_SAK_WRAPPED_LEN;
		os_memcpy(pos + NLA_HDRLEN, sa->pkey->wrapped_key, OB_SAK_WRAPPED_LEN);
		pos += NLA_ALIGN(attr->nla_len);

		attr = (struct nlattr *)pos;
		attr->nla_type = OB_MACSEC_SA_PKCS_KEK_HANDLE;
		attr->nla_len = NLA_HDRLEN + sizeof(u64);
		*(u64 *)(pos + NLA_HDRLEN) = sa->pkey->kek_handle;
		pos += NLA_ALIGN(attr->nla_len);
	}
#else
	if (sa->pkey && sa->pkey->key) {
		attr = (struct nlattr *)pos;
		attr->nla_type = OB_MACSEC_SA_ATTR_KEY;
		attr->nla_len = NLA_HDRLEN + sa->pkey->key_len;
		os_memcpy(pos + NLA_HDRLEN, sa->pkey->key, sa->pkey->key_len);
		pos += NLA_ALIGN(attr->nla_len);
	}
#endif

	nested_sa->nla_type = OB_MACSEC_ATTR_SA_CONFIG | NLA_F_NESTED;
	nested_sa->nla_len = pos - buf;

	return omni_macsec_send_cmd(drv, OB_MACSEC_CMD_CREATE_TX_SA, nested_sa);
}

static int macsec_omni_delete_transmit_sa(void *priv, struct transmit_sa *sa)
{
	struct macsec_omni_drv_data *drv = priv;
	char buf[128];
	struct nlattr *nested_sa = (struct nlattr *)buf;
	char *pos = buf + NLA_HDRLEN;

	struct nlattr *attr = (struct nlattr *)pos;
	attr->nla_type = OB_MACSEC_SA_ATTR_AN;
	attr->nla_len = NLA_HDRLEN + sizeof(u8);
	*(u8 *)(pos + NLA_HDRLEN) = sa->an;
	pos += NLA_ALIGN(attr->nla_len);

	nested_sa->nla_type = OB_MACSEC_ATTR_SA_CONFIG | NLA_F_NESTED;
	nested_sa->nla_len = pos - buf;

	return omni_macsec_send_cmd(drv, OB_MACSEC_CMD_DIS_TX_SA, nested_sa);
}

static int macsec_omni_enable_transmit_sa(void *priv, struct transmit_sa *sa)
{
	struct macsec_omni_drv_data *drv = priv;
	char buf[128];
	struct nlattr *nested_sa = (struct nlattr *)buf;
	char *pos = buf + NLA_HDRLEN;

	struct nlattr *attr = (struct nlattr *)pos;
	attr->nla_type = OB_MACSEC_SA_ATTR_AN;
	attr->nla_len = NLA_HDRLEN + sizeof(u8);
	*(u8 *)(pos + NLA_HDRLEN) = sa->an;
	pos += NLA_ALIGN(attr->nla_len);

	nested_sa->nla_type = OB_MACSEC_ATTR_SA_CONFIG | NLA_F_NESTED;
	nested_sa->nla_len = pos - buf;

	return omni_macsec_send_cmd(drv, OB_MACSEC_CMD_EN_TX_SA, nested_sa);
}

static int macsec_omni_disable_transmit_sa(void *priv, struct transmit_sa *sa)
{
	return macsec_omni_delete_transmit_sa(priv, sa);
}

const struct wpa_driver_ops wpa_driver_macsec_omni_ops = {
	.name = "macsec_omni",
	.desc = "Omni SoC MACsec Hardware Offload driver",
	.get_ssid = driver_wired_get_ssid,
	.get_bssid = driver_wired_get_bssid,
	.get_capa = driver_wired_get_capa,
	.init = macsec_omni_wpa_init,
	.deinit = macsec_omni_wpa_deinit,

	.macsec_init = macsec_omni_macsec_init,
	.macsec_deinit = macsec_omni_macsec_deinit,
	.macsec_get_capability = macsec_omni_get_capability,
	.enable_protect_frames = macsec_omni_enable_protect_frames,
	.enable_encrypt = macsec_omni_enable_encrypt,
	.set_replay_protect = macsec_omni_set_replay_protect,
	.set_offload = macsec_omni_set_offload,
	.set_current_cipher_suite = macsec_omni_set_current_cipher_suite,
	.enable_controlled_port = macsec_omni_enable_controlled_port,
	.get_receive_lowest_pn = macsec_omni_get_receive_lowest_pn,
	.set_receive_lowest_pn = macsec_omni_set_receive_lowest_pn,
	.get_transmit_next_pn = macsec_omni_get_transmit_next_pn,
	.set_transmit_next_pn = macsec_omni_set_transmit_next_pn,
	.create_receive_sc = macsec_omni_create_receive_sc,
	.delete_receive_sc = macsec_omni_delete_receive_sc,
	.create_receive_sa = macsec_omni_create_receive_sa,
	.delete_receive_sa = macsec_omni_delete_receive_sa,
	.enable_receive_sa = macsec_omni_enable_receive_sa,
	.disable_receive_sa = macsec_omni_disable_receive_sa,
	.create_transmit_sc = macsec_omni_create_transmit_sc,
	.delete_transmit_sc = macsec_omni_delete_transmit_sc,
	.create_transmit_sa = macsec_omni_create_transmit_sa,
	.delete_transmit_sa = macsec_omni_delete_transmit_sa,
	.enable_transmit_sa = macsec_omni_enable_transmit_sa,
	.disable_transmit_sa = macsec_omni_disable_transmit_sa,
};
