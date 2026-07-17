#!/usr/bin/env python3

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read_text(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8", errors="ignore")


class ZkWorkPlanContractTests(unittest.TestCase):
    def test_plan_route_and_public_interfaces_are_wired(self):
        header = read_text("Core/Src/LampProtocolLib/mqtt_zk_protocol.h")
        json_protocol = read_text("Core/Src/LampProtocolLib/Json_Protocol.c")

        self.assertIn('#define ZK_SV_PLAN              "plan"', header)
        self.assertIn("boolean_en zk_handle_plan_message", header)
        self.assertIn("void zk_apply_plan_brightness(int brightness);", header)
        self.assertIn("zk_response_dt_builder_t", header)
        self.assertIn("int zk_publish_response_with_dt", header)
        self.assertIn("boolean_en zk_dispatch_message", header)
        self.assertIn("zk_dispatch_message(root, header)", json_protocol)

    def test_plan_store_supports_protocol_limits(self):
        source = read_text("Core/Src/LampProtocolLib/zk_work_plan.c")
        plan_header = read_text("Core/Src/LampProtocolLib/zk_work_plan.h")

        self.assertIn("#define ZK_PLAN_MAX_COUNT       8", plan_header)
        self.assertIn("#define ZK_PLAN_MAX_JOBS        2", plan_header)
        self.assertIn("#define ZK_PLAN_MAX_ACTIONS     6", plan_header)
        self.assertIn("0x08006000", source)
        self.assertIn("0x08007800", source)
        self.assertIn("job_count > ZK_PLAN_MAX_JOBS", source)
        self.assertNotIn("job_count != 1", source)

    def test_plan_validation_keeps_week_and_brightness_contracts(self):
        source = read_text("Core/Src/LampProtocolLib/zk_work_plan.c")

        self.assertIn("strlen(text) != 7", source)
        self.assertIn("bri == 0 || (bri >= 10 && bri <= 100)", source)
        self.assertIn("timetp->valueint < 0 || timetp->valueint > 1", source)
        self.assertIn("plan_type == 1 && out->timetp != 0", source)
        self.assertIn("strncmp(text, \"FFFF\", 4)", source)

    def test_plan_read_operations_are_supported(self):
        source = read_text("Core/Src/LampProtocolLib/zk_work_plan.c")

        self.assertIn("strcmp(header->ct, ZK_CT_READ)", source)
        self.assertIn("zk_plan_handle_read(dt, header)", source)
        self.assertIn("strcmp(do_node->valuestring, \"nid\")", source)
        self.assertIn("strcmp(do_node->valuestring, \"sr\")", source)
        self.assertIn("zk_plan_build_sunriset_dt", source)
        self.assertIn("cJSON_GetObjectItem(dt, \"now\")", source)
        self.assertIn("cJSON_GetObjectItem(dt, \"id\")", source)

    def test_plan_delete_uses_protocol_write_del_array(self):
        source = read_text("Core/Src/LampProtocolLib/zk_work_plan.c")

        self.assertIn("cJSON_GetObjectItem(dt, \"del\")", source)
        self.assertIn("!cJSON_IsArray(del)", source)
        self.assertIn("cJSON_GetArraySize(del)", source)
        self.assertIn("zk_plan_delete(item->valueint)", source)
        self.assertIn("zk_publish_error_response(header, del_err)", source)

    def test_plan_query_response_shapes_are_serialized(self):
        source = read_text("Core/Src/LampProtocolLib/zk_work_plan.c")

        self.assertIn("zk_plan_record_to_json", source)
        self.assertIn("cJSON_AddItemToObject(dt, \"nid\", array)", source)
        self.assertIn("cJSON_AddItemToObject(dt, \"plan\", plan)", source)
        self.assertIn("cJSON_AddStringToObject(plan, \"sDate\"", source)
        self.assertIn("cJSON_AddStringToObject(plan, \"eDate\"", source)
        self.assertIn("cJSON_AddItemToObject(plan, \"jobs\", jobs)", source)
        self.assertIn("use_duration ? \"dTime\" : \"time\"", source)
        self.assertIn("(job->timetp == 1) ? \"dTime\" : \"time\"", source)
        self.assertIn("cJSON_AddItemToObject(job_json, \"bri\", bri_array)", source)

    def test_type2_sunrise_sunset_period_plan_uses_protocol_dtime(self):
        source = read_text("Core/Src/LampProtocolLib/zk_work_plan.c")

        self.assertIn("zk_sunriset_get(&sr_minute, &ss_minute)", source)
        self.assertIn("zk_plan_parse_duration", source)
        self.assertIn("value < 1 || value > 780", source)
        self.assertIn("schedule_array = cJSON_GetObjectItem(job, use_duration ? \"dTime\" : \"time\")", source)
        self.assertIn("offset_minute += (int)job->actions[offset_index].minute", source)
        self.assertNotIn("job->timetp == 2", source)

    def test_plan_now_query_uses_protocol_cns_and_empty_dt_when_no_match(self):
        source = read_text("Core/Src/LampProtocolLib/zk_work_plan.c")

        self.assertIn("cJSON_GetObjectItem(dt, \"now\")", source)
        self.assertIn("now_node->valueint < 1 || now_node->valueint > 2", source)
        self.assertIn("zk_plan_find_current_match_for_cns(&match, now_node->valueint)", source)
        self.assertIn("job->cns_mask & (1U << (cns - 1))", source)
        self.assertIn("zk_plan_publish_dt_response(header, NULL, NULL)", source)
        self.assertNotIn("static int zk_plan_build_now_dt", source)

    def test_main_uses_new_plan_executor_not_legacy_offline_executor(self):
        main_c = read_text("Core/Src/main.c")

        self.assertIn("zk_work_plan_init();", main_c)
        self.assertIn("zk_work_plan_process();", main_c)
        for line in main_c.splitlines():
            if "Work_offline_dimming_process();" in line:
                self.assertTrue(line.strip().startswith("//"), line)


if __name__ == "__main__":
    unittest.main()
