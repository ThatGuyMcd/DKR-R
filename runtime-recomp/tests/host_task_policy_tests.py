import copy
import json
from pathlib import Path
import struct
import sys
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from host_task_policy import verify_host_task_policy


class HostTaskPolicyTests(unittest.TestCase):
    def fixture(self, revision):
        policy = json.loads((ROOT / f"runtime-recomp/dkr.us.v{revision}.recomp-policy.json").read_text())
        hook = next(h for h in policy["functionHooks"] if "dkr_scheduler_keep_host_tasks" in h["text"])
        pc = int(hook["beforeVram"], 0)
        target, sp, dp = ((0x80079818, 0xE754, 0xE758) if revision == 77 else
                          (0x80079C68, 0xECD4, 0xECD8))
        words = {pc: 0x0C000000 | ((target >> 2) & 0x03FFFFFF), pc+4: 0x02402025,
                 target+0x30: 0x3C03800E, target+0x34: 0x24630000 | sp,
                 target+0x58: 0x3C08800E, target+0x5C: 0x8D080000 | dp,
                 target+0x74: 0x2941000B, target+0xBC: 0x2981000B}
        data = bytearray(0x400)
        for address, word in words.items(): struct.pack_into(">I", data, address-pc, word)
        return policy, hook, pc, data, words

    def test_reviewed_revisions(self):
        for revision in (77, 80):
            policy, _, pc, data, _ = self.fixture(revision)
            with patch("host_task_policy.elf_sections", return_value=[(pc, data)]):
                verify_host_task_policy(policy, None)

    def test_changed_instructions_fail_closed(self):
        for revision in (77, 80):
            policy, _, pc, data, words = self.fixture(revision)
            for address in words:
                corrupted = bytearray(data)
                corrupted[address-pc+3] ^= 1
                with self.subTest(revision=revision, address=address), patch(
                        "host_task_policy.elf_sections", return_value=[(pc, corrupted)]):
                    with self.assertRaises(ValueError): verify_host_task_policy(policy, None)

    def test_wrong_owner_register_counter_and_duplicate(self):
        for revision in (77, 80):
            for modification in ("owner", "register", "counter", "duplicate"):
                policy, hook, _, _, _ = self.fixture(revision)
                if modification == "owner": hook["function"] = "__scHandleRSP"
                if modification == "register": hook["text"] = hook["text"].replace("ctx->r18", "ctx->r16")
                if modification == "counter": hook["text"] = hook["text"].replace("0x800D", "0x800E")
                if modification == "duplicate": policy["functionHooks"].append(copy.deepcopy(hook))
                with self.subTest(revision=revision, modification=modification):
                    with self.assertRaises(ValueError): verify_host_task_policy(policy, None)

    def test_unrelated_policy_unchanged(self):
        verify_host_task_policy({"functionHooks": []}, None)


if __name__ == "__main__": unittest.main()
