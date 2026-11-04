#include "db_proto.h"

#include <string.h>

#include "db_json.h"

db_err_t db_proto_parse_image_hdr(const uint8_t *buf, size_t len, db_image_hdr_t *out)
{
    if (len < DB_IMAGE_HDR_LEN || buf[0] != DB_MSG_IMAGE) {
        return DB_ERR_PROTO;
    }
    *out = (db_image_hdr_t){
        .display = buf[1],
        .rect = {db_get_u16(buf + 2), db_get_u16(buf + 4), db_get_u16(buf + 6), db_get_u16(buf + 8)},
        .seq = db_get_u32(buf + 10),
    };
    return (out->rect.w == 0 || out->rect.h == 0) ? DB_ERR_PROTO : DB_OK;
}

db_err_t db_proto_parse_time(const uint8_t *buf, size_t len, uint64_t *server_time_us)
{
    if (len < DB_TIME_LEN || buf[0] != DB_MSG_TIME) {
        return DB_ERR_PROTO;
    }
    *server_time_us = db_get_u64(buf + 8);
    return DB_OK;
}

void db_proto_write_ack(uint8_t out[DB_ACK_LEN], db_ack_result_t result, uint8_t display, uint32_t seq,
                        uint64_t t_recv, uint64_t t_start, uint64_t t_done)
{
    memset(out, 0, DB_ACK_LEN);
    out[0] = DB_MSG_ACK;
    out[1] = (uint8_t)result;
    out[2] = display;
    db_put_u32(out + 4, seq);
    db_put_u64(out + 8, t_recv);
    db_put_u64(out + 16, t_start);
    db_put_u64(out + 24, t_done);
}

void db_proto_write_time_reply(uint8_t out[DB_TIME_REPLY_LEN], uint64_t server_time_us,
                               uint64_t bridge_time_us)
{
    memset(out, 0, DB_TIME_REPLY_LEN);
    out[0] = DB_MSG_TIME_REPLY;
    db_put_u64(out + 8, server_time_us);
    db_put_u64(out + 16, bridge_time_us);
}

void db_proto_write_input(uint8_t out[DB_INPUT_LEN], const db_input_t *ev)
{
    memset(out, 0, DB_INPUT_LEN);
    out[0] = DB_MSG_INPUT;
    out[1] = (uint8_t)ev->kind;
    out[2] = ev->index;
    out[3] = ev->display;
    db_put_u16(out + 4, (uint16_t)ev->value);
    db_put_u16(out + 6, ev->x);
    db_put_u16(out + 8, ev->y);
    db_put_u16(out + 10, ev->x2);
    db_put_u16(out + 12, ev->y2);
    db_put_u64(out + 24, ev->t_us);
}

static const char *const target_kinds[] = {"full", "key", "strip"};

static void write_rect(db_json_t *j, db_rect_t r)
{
    db_json_int(j, "x", r.x);
    db_json_int(j, "y", r.y);
    db_json_int(j, "w", r.w);
    db_json_int(j, "h", r.h);
}

static void write_display(db_json_t *j, uint8_t id, const db_display_t *d)
{
    db_json_obj_open(j, NULL);
    db_json_int(j, "id", id);
    db_json_int(j, "w", d->width);
    db_json_int(j, "h", d->height);
    db_json_str(j, "format", d->format == DB_FORMAT_JPEG ? "jpeg" : "bmp");
    db_json_int(j, "rotate", d->rotate);
    db_json_bool(j, "flip_x", d->flip_x);
    db_json_bool(j, "flip_y", d->flip_y);
    db_json_arr_open(j, "targets");
    for (uint8_t i = 0; i < d->target_count; i++) {
        const db_target_t *t = &d->targets[i];
        db_json_obj_open(j, NULL);
        db_json_str(j, "kind", target_kinds[t->kind]);
        if (t->kind == DB_TARGET_KEY) {
            db_json_int(j, "index", t->index);
        }
        write_rect(j, t->rect);
        if (t->partial) {
            db_json_bool(j, "partial", true);
        }
        db_json_obj_close(j);
    }
    db_json_arr_close(j);
    db_json_obj_close(j);
}

size_t db_proto_device_json(const db_device_info_t *info, char *buf, size_t cap)
{
    db_json_t j;
    db_json_begin(&j, buf, cap);
    db_json_obj_open(&j, NULL);
    db_json_str(&j, "t", "device");
    db_json_bool(&j, "present", info != NULL);
    if (info) {
        db_json_str(&j, "vendor", info->vendor);
        db_json_str(&j, "model", info->model);
        db_json_int(&j, "vid", info->vid);
        db_json_int(&j, "pid", info->pid);
        db_json_str(&j, "serial", info->serial);
        db_json_str(&j, "fw", info->firmware);
        db_json_bool(&j, "tested", info->tested);
        db_json_int(&j, "keys", info->key_count);
        db_json_int(&j, "encoders", info->encoder_count);
        db_json_arr_open(&j, "displays");
        for (uint8_t i = 0; i < info->display_count; i++) {
            write_display(&j, i, &info->displays[i]);
        }
        db_json_arr_close(&j);
        db_json_arr_open(&j, "touch");
        for (uint8_t i = 0; i < info->touch_count; i++) {
            db_json_obj_open(&j, NULL);
            db_json_int(&j, "display", info->touch[i].display);
            write_rect(&j, info->touch[i].rect);
            db_json_obj_close(&j);
        }
        db_json_arr_close(&j);
    }
    db_json_obj_close(&j);
    return db_json_end(&j);
}
