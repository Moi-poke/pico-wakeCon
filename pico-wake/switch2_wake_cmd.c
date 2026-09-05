#include "switch2_wake_cmd.h"
#include "switch2_wake_util.h"

#include <string.h>

static const uint8_t device_public_key[16] = {
    0x5c,0xf6,0xee,0x79,0x2c,0xdf,0x05,0xe1,
    0xba,0x2b,0x63,0x25,0xc4,0x1a,0x5f,0x10
};

static const uint8_t response_1101[] = { 0x01,0x00,0x00,0x00 };
static const uint8_t response_1103[] = {
    0x01,0x20,0x03,0x00,0x00,0x0a,0xe8,0x1c,0x3b,0x79,0x7d,0x8b,0x3a,0x0a,
    0xe8,0x9c,0x42,0x58,0xa0,0x0b,0x42,0x0a,0xe8,0x9c,0x41,0x58,0xa0,0x0b,0x41
};
static const uint8_t response_1801[] = { 0x00,0x00,0x40,0xf0,0x00,0x00,0x60,0x00 };
static const uint8_t firmware_info[] = {
    0x02,0x01,0x04,0x02,0x0c,0x00,0x00,0x00,0x00,0x02,0x03,0x00
};

static uint16_t request_offset(uint16_t handle, const uint8_t *buffer, uint16_t size) {
    uint16_t pos=0u;
    if (handle==WAKE_CMD_HANDLE_RUMBLE && size>=WAKE_CMD_PAD_0016+4u) {
        return WAKE_CMD_PAD_0016;
    }
    if (handle==WAKE_CMD_HANDLE_BASIC) return 0u;
    /* 0x0018 は chunk 形式。実物互換の保険として先頭ゼロを飛ばす。 */
    while (pos<size && buffer[pos]==0u) pos++;
    return pos;
}

static bool queue_response(switch2_wake_cmd_t *s, uint8_t cmd, uint8_t sub,
                           const uint8_t *data, uint16_t data_len) {
    uint16_t pos=WAKE_CMD_RESPONSE_PAD;
    if (s==NULL || s->port.emit_response==NULL) return false;
    if ((uint32_t)pos+8u+data_len>sizeof(s->response)) return false;
    memset(s->response,0,sizeof(s->response));
    s->response[pos++]=cmd;
    s->response[pos++]=0x01u;
    s->response[pos++]=0x01u;
    s->response[pos++]=sub;
    s->response[pos++]=0x10u;
    s->response[pos++]=0x78u;
    s->response[pos++]=0x00u;
    s->response[pos++]=0x00u;
    if (data_len>0u && data!=NULL) { memcpy(&s->response[pos],data,data_len); pos=(uint16_t)(pos+data_len); }
    s->last_response_len=pos;
    s->responded++;
    s->port.emit_response(s->port.ctx,s->response,pos);
    if (s->port.observe!=NULL) s->port.observe(s->port.ctx,cmd,sub,0u,pos);
    return true;
}

static void aes_done(void *done_ctx, const uint8_t out[16]) {
    switch2_wake_cmd_t *s=(switch2_wake_cmd_t *)done_ctx;
    uint8_t data[17];
    if (s==NULL || !s->aes_busy || out==NULL) return;
    memcpy(s->aes_out,out,16u);
    data[0]=0x01u; memcpy(&data[1],out,16u);
    s->aes_busy=false;
    (void)queue_response(s,0x15u,0x02u,data,sizeof(data));
    memset(data,0,sizeof(data));
}

static switch2_wake_cmd_result_t pairing(switch2_wake_cmd_t *s, uint8_t sub,
    const uint8_t *data, uint16_t len) {
    uint8_t response[17];
    uint8_t local[6];
    unsigned int i;
    memset(response,0,sizeof(response));
    switch (sub) {
    case 0x01u:
        if (s->port.get_local_addr==NULL || !s->port.get_local_addr(s->port.ctx,local)) return WAKE_CMD_PORT_ERROR;
        response[0]=0x01u; response[1]=0x04u; response[2]=0x01u;
        for (i=0u;i<6u;i++) response[3u+i]=local[5u-i];
        return queue_response(s,0x15u,sub,response,9u)?WAKE_CMD_OK:WAKE_CMD_PORT_ERROR;
    case 0x04u:
        if (len<17u) return WAKE_CMD_SHORT;
        for (i=0u;i<16u;i++) s->ltk[i]=data[1u+i]^device_public_key[i];
        s->ltk_valid=true; response[0]=0x01u; memcpy(&response[1],device_public_key,16u);
        return queue_response(s,0x15u,sub,response,17u)?WAKE_CMD_OK:WAKE_CMD_PORT_ERROR;
    case 0x02u:
        if (len<17u || !s->ltk_valid) return WAKE_CMD_SHORT;
        if (s->aes_busy) return WAKE_CMD_BUSY;
        if (s->port.start_aes==NULL) return WAKE_CMD_PORT_ERROR;
        switch2_wake_reverse16(s->ltk,s->aes_key); switch2_wake_reverse16(&data[1],s->aes_plain);
        s->aes_busy=true;
        if (!s->port.start_aes(s->port.ctx,s->aes_key,s->aes_plain,aes_done,s)) { s->aes_busy=false; return WAKE_CMD_PORT_ERROR; }
        return WAKE_CMD_OK;
    case 0x03u:
        if (!s->ltk_valid || !s->peer_valid || s->port.store_bond==NULL) return WAKE_CMD_PORT_ERROR;
        if (!s->port.store_bond(s->port.ctx,s->peer_type,s->peer,s->ltk)) return WAKE_CMD_PORT_ERROR;
        response[0]=0x01u;
        return queue_response(s,0x15u,sub,response,1u)?WAKE_CMD_OK:WAKE_CMD_PORT_ERROR;
    default:
        return queue_response(s,0x15u,sub,NULL,0u)?WAKE_CMD_OK:WAKE_CMD_PORT_ERROR;
    }
}

static switch2_wake_cmd_result_t dispatch(switch2_wake_cmd_t *s, const uint8_t *pkt, uint16_t len) {
    uint8_t cmd,sub; const uint8_t *data; uint16_t dlen; uint8_t zeros[24];
    uint8_t one=0u; uint8_t four[4]={0,0,0,0};
    if (len<WAKE_CMD_REQUEST_HEADER_SIZE) return WAKE_CMD_SHORT;
    cmd=pkt[0]; sub=pkt[3]; data=&pkt[8]; dlen=(uint16_t)(len-8u);
    s->last_cmd=cmd; s->last_sub=sub;
    if (s->port.observe!=NULL) s->port.observe(s->port.ctx,cmd,sub,len,0u);
    if (cmd==0x15u) return pairing(s,sub,data,dlen);
    if (cmd==0x01u && sub==0x0cu) { static const uint8_t v[]={0x61,0x12,0x50,0x10}; return queue_response(s,cmd,sub,v,4u)?WAKE_CMD_OK:WAKE_CMD_PORT_ERROR; }
    if (cmd==0x02u && sub==0x04u && dlen>=8u && s->port.read_memory!=NULL) {
        uint8_t out[128]; uint8_t n=data[0]; uint32_t a;
        if (n>120u || dlen<8u) return WAKE_CMD_SHORT;
        memcpy(out,data,8u); out[1]=0u; a=(uint32_t)data[4]|((uint32_t)data[5]<<8)|((uint32_t)data[6]<<16);
        if (!s->port.read_memory(s->port.ctx,a,&out[8],n)) return queue_response(s,cmd,sub,NULL,0u)?WAKE_CMD_OK:WAKE_CMD_PORT_ERROR;
        return queue_response(s,cmd,sub,out,(uint16_t)(8u+n))?WAKE_CMD_OK:WAKE_CMD_PORT_ERROR;
    }
    if (cmd==0x07u) return queue_response(s,cmd,sub,&one,1u)?WAKE_CMD_OK:WAKE_CMD_PORT_ERROR;
    if (cmd==0x0cu) {
        if (sub==0x04u) { s->registration_complete=true; if (s->port.on_registered!=NULL) s->port.on_registered(s->port.ctx); }
        return queue_response(s,cmd,sub,four,4u)?WAKE_CMD_OK:WAKE_CMD_PORT_ERROR;
    }
    if (cmd==0x10u) return queue_response(s,cmd,sub,firmware_info,sizeof(firmware_info))?WAKE_CMD_OK:WAKE_CMD_PORT_ERROR;
    if (cmd==0x11u && sub==0x01u) return queue_response(s,cmd,sub,response_1101,sizeof(response_1101))?WAKE_CMD_OK:WAKE_CMD_PORT_ERROR;
    if (cmd==0x11u && sub==0x03u) return queue_response(s,cmd,sub,response_1103,sizeof(response_1103))?WAKE_CMD_OK:WAKE_CMD_PORT_ERROR;
    if (cmd==0x16u) { memset(zeros,0,sizeof(zeros)); return queue_response(s,cmd,sub,zeros,sizeof(zeros))?WAKE_CMD_OK:WAKE_CMD_PORT_ERROR; }
    if (cmd==0x18u && sub==0x01u) return queue_response(s,cmd,sub,response_1801,sizeof(response_1801))?WAKE_CMD_OK:WAKE_CMD_PORT_ERROR;
    if (cmd==0x18u && sub==0x03u) return queue_response(s,cmd,sub,data,dlen)?WAKE_CMD_OK:WAKE_CMD_PORT_ERROR;
    return queue_response(s,cmd,sub,NULL,0u)?WAKE_CMD_OK:WAKE_CMD_PORT_ERROR;
}

void switch2_wake_cmd_init(switch2_wake_cmd_t *s, const switch2_wake_cmd_port_t *port) {
    if (s == NULL) return;
    memset(s, 0, sizeof(*s));
    s->bond_index = -1;
    if (port != NULL) s->port = *port;
}

static void clear_pairing_secrets(switch2_wake_cmd_t *s) {
    if (s == NULL) return;
    memset(s->ltk, 0, sizeof(s->ltk));
    memset(s->aes_key, 0, sizeof(s->aes_key));
    memset(s->aes_plain, 0, sizeof(s->aes_plain));
    memset(s->aes_out, 0, sizeof(s->aes_out));
    memset(s->response, 0, sizeof(s->response));
    s->ltk_valid = false;
    s->aes_busy = false;
    s->registration_complete = false;
    s->last_response_len = 0u;
    s->bond_index = -1;
}

void switch2_wake_cmd_set_peer(switch2_wake_cmd_t *s, uint8_t type,
                               const uint8_t peer[6]) {
    if (s == NULL || peer == NULL) return;
    if (s->peer_valid &&
        ((s->peer_type != type) || (memcmp(s->peer, peer, 6u) != 0))) {
        clear_pairing_secrets(s);
    }
    s->peer_type = type;
    memcpy(s->peer, peer, 6u);
    s->peer_valid = true;
}

void switch2_wake_cmd_clear_pairing(switch2_wake_cmd_t *s) {
    if (s == NULL) return;
    clear_pairing_secrets(s);
    memset(s->peer, 0, sizeof(s->peer));
    s->peer_type = 0u;
    s->peer_valid = false;
}

switch2_wake_cmd_result_t switch2_wake_cmd_write(switch2_wake_cmd_t *s,uint16_t handle,const uint8_t *buffer,uint16_t size) {
    uint16_t pos; switch2_wake_cmd_result_t r;
    if (s==NULL || buffer==NULL) return WAKE_CMD_PORT_ERROR;
    if (handle!=WAKE_CMD_HANDLE_BASIC && handle!=WAKE_CMD_HANDLE_RUMBLE && handle!=WAKE_CMD_HANDLE_LARGE) return WAKE_CMD_IGNORED;
    pos=request_offset(handle,buffer,size); s->received++;
    if (pos>=size) { s->rejected++; return WAKE_CMD_SHORT; }
    r=dispatch(s,&buffer[pos],(uint16_t)(size-pos)); if (r!=WAKE_CMD_OK) s->rejected++; return r;
}

bool switch2_wake_cmd_get_ltk(const switch2_wake_cmd_t *s,uint8_t out[16]) {
    if (s == NULL || out == NULL || !s->ltk_valid) return false;
    switch2_wake_reverse16(s->ltk, out);
    return true;
}

bool switch2_wake_cmd_get_ltk_for_peer(const switch2_wake_cmd_t *s,
                                           uint8_t peer_type,
                                           const uint8_t peer[6],
                                           uint8_t out[16]) {
    if (s == NULL || peer == NULL || out == NULL || !s->peer_valid) return false;
    if ((s->peer_type != peer_type) || (memcmp(s->peer, peer, 6u) != 0)) return false;
    return switch2_wake_cmd_get_ltk(s, out);
}

uint16_t switch2_wake_cmd_fingerprint(const switch2_wake_cmd_t *s) {
    uint16_t crc=0xffffu; unsigned int i,b;
    if (s==NULL || !s->ltk_valid) return 0u;
    for (i=0u;i<16u;i++) { crc^=(uint16_t)s->ltk[i]<<8; for (b=0u;b<8u;b++) crc=(crc&0x8000u)?(uint16_t)((crc<<1)^0x1021u):(uint16_t)(crc<<1); }
    return crc;
}

bool switch2_wake_cmd_selftest_values(uint8_t ltk[16],uint8_t key[16],uint8_t plain[16]) {
    static const uint8_t a1[16]={0x35,0x03,0xe9,0x29,0x82,0x87,0x71,0x24,0xbe,0xa8,0x0c,0x66,0x46,0x15,0x83,0x4b};
    static const uint8_t a2[16]={0x6f,0xc6,0xdf,0x8a,0xd8,0xfe,0xdf,0x15,0xbb,0x8c,0x15,0xe9,0x1f,0x32,0x05,0x44};
    unsigned int i; if (ltk==NULL||key==NULL||plain==NULL) return false;
    for (i = 0u; i < 16u; i++) ltk[i] = a1[i] ^ device_public_key[i];
    switch2_wake_reverse16(ltk, key);
    switch2_wake_reverse16(a2, plain);
    return true;
}

const char *switch2_wake_cmd_result_name(switch2_wake_cmd_result_t r) {
    switch(r){case WAKE_CMD_OK:return "OK";case WAKE_CMD_IGNORED:return "IGNORED";case WAKE_CMD_SHORT:return "SHORT";case WAKE_CMD_BUSY:return "BUSY";default:return "PORT_ERROR";}
}
