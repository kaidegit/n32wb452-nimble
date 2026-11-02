#include <assert.h>
#include "../../../components/gatt_perf/gatt_perf.c"
static uint32_t mock_fraction_us;
uint32_t gatt_perf_time_us(void) { return mock_time * 1000 + mock_fraction_us; }
int hci_link_stats_get(uint16_t conn, struct hci_link_stats *out) { return 0; }

static int control(unsigned op, unsigned dir, unsigned size)
{
    struct os_mbuf om = {.data={1,1,dir,op,size,size>>8,0,0,100,0,0,0},.length=12};
    struct ble_gatt_access_ctxt ctxt={.op=BLE_GATT_ACCESS_OP_WRITE_CHR,.om=&om};
    return gatt_perf_access(7,gatt_perf_control_handle,&ctxt,NULL);
}

int main(void)
{
    gatt_perf_control_handle=5;gatt_perf_data_handle=6;
    gatt_perf_init();
    assert(control(3,1,20)==0 && mock_dle_requests==1);
    mock_dle_result=513;
    assert(control(3,1,20)==BLE_ATT_ERR_UNLIKELY && mock_dle_requests==2);
    mock_dle_result=0;
    assert(control(1,1,3)==BLE_ATT_ERR_UNLIKELY);
    assert(control(1,1,245)==BLE_ATT_ERR_UNLIKELY);
    assert(control(1,0,20)==BLE_ATT_ERR_UNLIKELY);
    assert(control(1,2,20)==BLE_ATT_ERR_UNLIKELY); /* notifications not enabled */
    mock_time=1000;
    assert(control(1,1,20)==0);
    assert(control(3,1,20)==BLE_ATT_ERR_UNLIKELY && mock_dle_requests==2);
    assert(control(1,1,20)==BLE_ATT_ERR_UNLIKELY); /* overlapping transfer */
    struct os_mbuf payload={.length=20};
    put_u32(payload.data,0);
    for(unsigned i=4;i<20;i++) payload.data[i]=payload_byte(0,i);
    struct ble_gatt_access_ctxt ctxt={.op=BLE_GATT_ACCESS_OP_WRITE_CHR,.om=&payload};
    mock_time=1025;
    assert(gatt_perf_access(7,6,&ctxt,NULL)==0);
    assert(bytes==20 && packets==1 && errors==0);
    /* A repeated sequence and a corrupted pattern must both be detected. */
    assert(gatt_perf_access(7,6,&ctxt,NULL)==0);
    put_u32(payload.data,2);payload.data[5]^=1;
    assert(gatt_perf_access(7,6,&ctxt,NULL)==0);
    assert(errors==2);
    assert(control(0,1,20)==0 && done && !running);
    assert(gatt_perf_access(7,6,&ctxt,NULL)==BLE_ATT_ERR_UNLIKELY);
    struct os_mbuf status={0};
    ctxt.op=BLE_GATT_ACCESS_OP_READ_CHR;ctxt.om=&status;
    assert(gatt_perf_access(7,5,&ctxt,NULL)==0);
    assert(get_u32(status.data+32)==513 && status.data[36]==1);
    assert(status.length==100 && status.data[0]==5 && status.data[4]==1 && status.data[7]==247);
    assert(get_u32(status.data+8)==60 && get_u32(status.data+16)==2);
    assert(get_u32(status.data+20)==25 && status.data[88]==0 && status.data[94]==12);
    assert(get_u32(status.data+96)==25000);

    struct ble_gap_event event={.type=BLE_GAP_EVENT_SUBSCRIBE,.subscribe={6,1}};
    gatt_perf_gap_event(&event);
    assert(control(1,2,20)==0);
    mock_time+=1;send_payloads(NULL);
    assert(mock_notify_count==8 && packets==8 && bytes==160);
    assert(get_u32(mock_tx.data)==7 && mock_tx.data[4]==payload_byte(7,4));
    mock_time+=100;send_payloads(NULL);
    assert(done && !running && !tx_pump.scheduled);
    assert(control(1,2,20)==0);
    event.type=BLE_GAP_EVENT_DISCONNECT;gatt_perf_gap_event(&event);
    assert(!running && !subscribed && !tx_pump.scheduled);

    /* A 32-bit microsecond wrap must not distort elapsed time. */
    mock_time=4294967;
    assert(control(1,1,4)==0);
    mock_time+=9;mock_fraction_us=234;
    payload.length=4;put_u32(payload.data,0);
    ctxt.op=BLE_GATT_ACCESS_OP_WRITE_CHR;ctxt.om=&payload;
    assert(gatt_perf_access(7,6,&ctxt,NULL)==0);
    status.length=0;ctxt.op=BLE_GATT_ACCESS_OP_READ_CHR;ctxt.om=&status;
    assert(gatt_perf_access(7,5,&ctxt,NULL)==0);
    assert(get_u32(status.data+20)==9 && get_u32(status.data+96)==9234);
    return 0;
}
