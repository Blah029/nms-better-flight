/*
 * Byte signatures. Sources: NMS.py master (monkeyman192) + our own RE of the
 * flight code. Each must match exactly once in NMS.exe .text.
 * Verified unique against Steam buildids 25233815 and 25320008.
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

/* Pilot check, inside cGcSpaceshipComponent::Update. The game runs
 * UpdateControlled when the ship's controller handle is valid and active - and
 * ALSO for corvettes (ship class 10) with nobody at the controls, so they keep
 * simulating while the player walks around inside or goes on EVA.
 * Primary site:
 *   lea rcx,[rsi+CTRL]; call IsValid; test al,al; je ..
 *   cmp byte [rsi+ACTIVE],0; jne controlled; cmp dword [rsi+..],3; je controlled
 *   CTRL disp32 @ +3, ACTIVE disp32 @ +18                                        */
#define SIG_PILOT_CHECK "48 8D 8E ? ? ? ? E8 ? ? ? ? 84 C0 74 ? 80 BE ? ? ? ? 00 0F 85 ? ? ? ? 83 BE ? ? ? ? 03 0F 84"
/* Second site:
 *   mov rax,[rsi+CTRL]; test rax,rax; je; cmp [rax],r14; je; cmp [rsi+ACTIVE],r14b; je
 *   movaps xmm1,xmm15; mov rcx,rsi; call UpdateControlled
 *   CTRL disp32 @ +3, ACTIVE disp32 @ +20, call rel32 @ +34                        */
#define SIG_PILOT_BRANCH "48 8B 86 ? ? ? ? 48 85 C0 74 ? 4C 39 30 74 ? 44 38 B6 ? ? ? ? 74 ? 41 0F 28 CF 48 8B CE E8"
