/*
 * Byte signatures. Sources: NMS.py master (monkeyman192) + our own RE of the
 * flight code. Each must match exactly once in NMS.exe .text.
 * Verified unique against Steam buildid 25233815.
 */
#pragma once

/* cGcSpaceshipComponent::UpdateControlled(this, float lfTimeStep) - hooked */
#define SIG_UPDATE_CONTROLLED "F3 0F 11 4C 24 ? 55 53 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 ? ? ? ? B8 ? ? ? ? E8 ? ? ? ? 48 2B E0 4C 8B A9"

/* cGcSpaceshipComponent::GetVelocity(this, cTkVector3 *out)
 *   mov rcx,[rcx+PHYSICS] (disp32 @ +9)   add rcx,RIGIDBODY (imm8 @ +19) */
#define SIG_GET_VELOCITY "40 53 48 83 EC ? 48 8B 89 ? ? ? ? 48 8B DA 48 83 C1"

/* cTkRigidBody::SetLinearVelocity(this, const cTkVector3 *v, bool bNoSync) */
#define SIG_SET_LINEAR_VELOCITY "48 89 5C 24 ? 57 48 83 EC ? 41 0F B6 F8 48 8B D9 4C 8B 81 ? ? ? ? 49 83 B8 ? ? ? ? ? 75 ? 48 8B 81 ? ? ? ? 48 85 C0 74 ? 80 08"

/* cTkRigidBody::GetTransform(this, out) -> pointer to rows right/up/at/pos...
 * Primary: a callsite inside UpdateControlled, exactly how the flight code
 * reads the ship's orientation:
 *   mov rcx,[rsi+PHYS]; lea rdx,[rbp+..]; add rcx,RB; call GetTransform (E8 @ +18)
 *   lea rdx,[rbp+..]; mov rcx,r13; movups xmm7,[rax]                            */
#define SIG_GET_TRANSFORM_CALLSITE "48 8B 8E ? ? ? ? 48 8D 95 ? ? ? ? 48 83 C1 ? E8 ? ? ? ? 48 8D 95 ? ? ? ? 49 8B CD 0F 10 38"
/* Fallback: the function itself.  mov rsi,[rcx+STATE] (disp32 @ +13) */
#define SIG_GET_TRANSFORM "48 89 74 24 10 57 48 83 EC 70 48 8B B1 90 02 00"
