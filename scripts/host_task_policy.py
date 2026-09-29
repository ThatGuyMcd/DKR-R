"""Qualify the project-owned retail watchdog hook against the input MIPS ELF."""
import struct
from compose_legacy_mod_policy import elf_sections


def verify_host_task_policy(policy, elf):
    hooks = [h for h in policy.get("functionHooks", [])
             if "dkr_scheduler_keep_host_tasks" in h.get("text", "")]
    if not hooks:
        return  # Older policies and unrelated pipeline fixtures remain valid.
    if len(hooks) != 1 or hooks[0]["function"] != "__scMain":
        raise ValueError("Host-task lifetime requires exactly one __scMain owner")
    hook = hooks[0]
    pc = int(hook["beforeVram"], 0)
    revisions = {
        0x80079648: (0x80079818, 0xE754, 0xE758),
        0x80079A98: (0x80079C68, 0xECD4, 0xECD8),
    }
    if pc not in revisions:
        raise ValueError("Unreviewed scheduler revision")
    target, sp_low, dp_low = revisions[pc]
    expected = ("extern void dkr_scheduler_keep_host_tasks(uint8_t*, uint32_t, uint32_t, uint32_t); "
                f"dkr_scheduler_keep_host_tasks(rdram, (uint32_t)ctx->r18, 0x{0x800D0000 + sp_low:08X}U, "
                f"0x{0x800D0000 + dp_low:08X}U);")
    if hook["text"] != expected:
        raise ValueError("Scheduler register/counter ownership changed")
    sections = elf_sections(elf)
    signatures = {
        pc: 0x0C000000 | ((target >> 2) & 0x03FFFFFF),  # jal __scHandleRetrace
        pc + 4: 0x02402025,  # delay slot: or a0,s2,zero
        target + 0x30: 0x3C03800E,  # lui v1,0x800E
        target + 0x34: 0x24630000 | sp_low,  # addiu v1,v1,SP counter
        target + 0x58: 0x3C08800E,  # lui t0,0x800E
        target + 0x5C: 0x8D080000 | dp_low,  # lw t0,DP counter(t0)
        target + 0x74: 0x2941000B,  # slti at,t2,11
        target + 0xBC: 0x2981000B,  # slti at,t4,11
    }
    for address, opcode in signatures.items():
        matches = [struct.unpack_from(">I", data, address - base)[0]
                   for base, data in sections if base <= address <= base + len(data) - 4]
        if matches != [opcode]:
            raise ValueError(f"Scheduler hook signature mismatch at {address:08X}")
