set(protected "runtime-recomp/RecompiledFuncs/")
file(READ "${ledger}" text)
string(REPLACE "\r\n" "\n" text "${text}")
file(WRITE "${ledger}" "${text}")
