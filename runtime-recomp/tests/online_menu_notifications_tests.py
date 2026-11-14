"""Keep the stock fallback aligned with the real, always-emitted menu hooks."""

import importlib.util
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("menu_producer", ROOT / "scripts/stage_magic_code_payload.py")
producer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(producer)


class MenuNotifications(unittest.TestCase):
    def test_every_emitted_option_pair_is_accepted_without_a_mod_owner(self):
        policy = (ROOT / "runtime-recomp/src/game/netplay/owned_menu_policy.h").read_text()
        upper = int(re.search(r"event\s*<=\s*(\d+)", policy)[1])
        emitted = set(range(18))
        for function, event in producer.TRACK_OPTIONS.items():
            body = f"RECOMP_FUNC void {function}(uint8_t* rdram, recomp_context* ctx) {{ }}"
            for owned in (False, True):
                wrapper = producer.wrap(function, body, 0x80000000, owned, "0x80000000U")
                # The no-code-hook fast branch must execute the same paired
                # notifications; disabling Magic Codes does not remove them.
                fast = wrapper.split("if(!dkr_code_hook_active(", 1)[1].split(" return; }", 1)[0]
                self.assertIn(f"ctx,{event}U,menu_fields,0U)", fast)
                self.assertIn(f"ctx,{event + 1}U,menu_fields,0U)", fast)
            emitted.update((event, event + 1))
        self.assertEqual(set(range(upper + 1)), emitted)
        self.assertEqual(producer.TRACK_OPTIONS["trackmenu_input"], 24)


if __name__ == "__main__":
    unittest.main()
