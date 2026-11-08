#pragma once

// The owned Track Select loader emulates thread30 on a separate guest stack.
// This is native engine scratch, NOT extra memory granted to code mods. Its
// O32 outgoing argument home area extends 64 bytes above the initial SP.
#define DKR_OWNED_PREVIEW_STACK_BEGIN 0x80FD0000U
#define DKR_OWNED_PREVIEW_STACK_TOP 0x80FE0000U
#define DKR_OWNED_O32_ARGUMENT_WORDS 16U
