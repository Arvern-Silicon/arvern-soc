#----------------------------------------------------------------------------
#          _    _           Family:    aRVern System IPs
#         / \__/ \          Module:    waivers.tcl
#        /   /\   \         --------------------------------------------
#    ===/   /=========      Copyright: (c) 2026, aRVern-dev
#      /   / RV \   \       Contact:   arvernsilicon@gmail.com
#     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
#
# SPDX-License-Identifier: BSD-3-Clause
# Full license text is available in the LICENSE file at the repository root.
#----------------------------------------------------------------------------
# File Name          : waivers.tcl
# Module Description : Design-specific VC Static lint waivers for chip_example.
#----------------------------------------------------------------------------
# Sourced by vc_lint.tcl after check_hdl, so waived violations are classified
# as they are found. Waived items still appear in report.lint_waived.txt.
#
# SCOPE: this elaboration contains the arvern core and every IP, so the core's
# and the IPs' already-adjudicated waivers are inherited here -- the findings
# they cover are identical, only reached through a different top. The block
# marked SoC-SPECIFIC below is what this file adds. Keep the inherited entries
# in step with arvern/lint/vc_static/waivers.tcl and
# arvern-ips/ahb_aclint/lint/vc_static/waivers.tcl.
#
# `waive_hdl -not_applied` runs after the check and run_vclint reports the count
# as `stale=` -- a non-zero stale count means a waiver matched nothing and
# should be removed or re-scoped. The conditional gates below exist to keep that
# signal honest: a waiver registered in a configuration where its RTL is not
# even elaborated would read as stale every run.
#
# RTL_PARAM_* come from results/rtl_params.tcl, harvested by run_vclint out of
# chip_example.v. If a gate below never fires, check that file first.
#----------------------------------------------------------------------------

# ---------------------------------------------------------------------------
# INHERITED from arvern/lint/vc_static/waivers.tcl
#
# The aRVern *_unused sink-wire convention: a wire whose name ends in _unused
# exists precisely to document a signal that is deliberately not consumed in a
# given configuration (muldiv state at M_EXTENSION=0, HPM regs at ZIHPM_NR=0,
# ...). An unloaded net is the intended state, not a finding.
#
# Scope note: waives by signal name only, so it covers every tag that can fire
# on such a net (CONN_NET_UNLOADED, CONN_INTERNAL_NET_UNLOADED,
# CONN_PORT_UNLOADED). Nothing without the suffix is waived.
# ---------------------------------------------------------------------------
waive_hdl -add unused_sink_wires \
          -comment "Deliberate *_unused sink wires: unloaded by design (aRVern convention)" \
          -filter {Signal=~*_unused*}

# ---------------------------------------------------------------------------
# INHERITED from arvern.
#
# arv_dff / arv_dff_sinit with a non-zero RST_VAL: bits that reset to 1 infer an
# async SET and bits that reset to 0 an async RESET, both driven from the same
# hresetn_i. That is exactly what the rule describes, and it is the normal and
# correct implementation of a flop with a non-zero reset value.
#
# WIDER than arvern's copy, deliberately: the SoC also elaborates the IP-side
# primitive arv_ipdff (arvern-ips/arv_primitives), which is the same flop with
# the same RST_VAL parameter, and the DTM's TAP instantiates it with non-zero
# reset values by spec -- u_ir resets to IR_IDCODE, u_state to S_TLR. The
# pattern arv_*dff* covers arv_dff, arv_dff_sinit, arv_ipdff and
# arv_ipdff_sinit and nothing else.
# ---------------------------------------------------------------------------
waive_hdl -add dff_nonzero_rstval_setreset \
          -comment "arv_dff / arv_ipdff with non-zero RST_VAL: mixed set/reset from one reset is by design" \
          -tag CODING_TREE_SETRST_ORIG \
          -filter {Module=~arv_*dff*}

# ---------------------------------------------------------------------------
# INHERITED from arvern.
#
# Deliberate post-reset one-shot flops: en_i(1'b1) with a tied d_i, so the flop
# loads its constant on the first clock and holds it. The tied input IS the
# mechanism (arv_fetch u_init_pc, arv_debug_dm u_hart_alive, arv_csr_cntr,
# arv_csr_debug reset_halt_arm_q).
#
# Waived by tag rather than per-instance: compression reports only one at a
# time, so an instance-scoped waiver just promotes the next one into view. Also
# catches flops tied off by parameter folding, which is why it is deliberately
# NOT gated on the reset style -- see the note in arvern's copy.
# ---------------------------------------------------------------------------
waive_hdl -add tied_input_oneshot_ff \
          -comment "Post-reset one-shot flops: tied d_i with en_i=1 is the intended mechanism" \
          -tag SYN_FF_CONST_INP

# ---------------------------------------------------------------------------
# INHERITED from arvern.
#
# arv_dff_sinit carries BOTH a reset (rst_n_i) and a synchronous init (sinit_i),
# and for the Debug Module both are necessarily live signals:
#   rst_n_i = dbgresetn_i          -- physical debug-domain reset
#   sinit_i = dm_sinit = ~dmactive -- the debugger's software reset
# The RISC-V Debug spec requires the debugger to be able to clear DM-side state
# via dmactive without a physical reset, so neither input can be tied off.
# Priority is explicit in the RTL (rst_n_i, then sinit_i, then en_i), so the
# race the rule guards against cannot occur here.
#
# CONDITIONAL: the rule only fires at ASYNC_RST_EN=0 -- with an async reset the
# tool sees one async control plus ordinary clock-sampled logic. Gated on
# DEBUG_EN too, since every arv_dff_sinit instance lives in the debug modules.
# ---------------------------------------------------------------------------
if {[info exists RTL_PARAM_ASYNC_RST_EN] && $RTL_PARAM_ASYNC_RST_EN == 0
    && [info exists RTL_PARAM_DEBUG_EN] && $RTL_PARAM_DEBUG_EN != 0} {
    waive_hdl -add dff_sinit_dual_force \
              -comment "arv_dff_sinit: reset and sync-init are both live by Debug-spec requirement" \
              -tag SEQ_RST_CONST_CONN \
              -filter {Module=~arv_dff_sinit*}
    puts "\[vc_lint\] sync-reset build: SEQ_RST_CONST_CONN waiver registered for arv_dff_sinit"
}

# ---------------------------------------------------------------------------
# INHERITED from arvern-ips/ahb_aclint.
#
# arv_synchronizer's SECOND stage carries no reset under ASYNC_RST_EN=0, by
# design. A synchronous reset is a 2:1 mux on the D pin, and a mux in the
# meta_q -> sync_q path eats directly into the metastability settling window --
# MTBF is exponential in the time available there. The reset mux is therefore
# kept on the FIRST stage only, whose D is the asynchronous input and has no
# setup relationship to lose.
#
# CONDITIONAL on the reset style: in async builds both stages have a real async
# reset and the rule never fires.
#
# Scoped by signal suffix rather than by the ACLINT's single instance path --
# the SoC instantiates arv_synchronizer in several places (ACLINT LF domain,
# debug transport), and they share the same justification.
# ---------------------------------------------------------------------------
if {[info exists RTL_PARAM_ASYNC_RST_EN] && $RTL_PARAM_ASYNC_RST_EN == 0} {
    waive_hdl -add sync_stage2_no_reset \
              -comment "arv_synchronizer sync_q: unreset by design under ASYNC_RST_EN=0 -- a reset mux in the meta->sync path would degrade MTBF" \
              -filter {Tag=~CODING_FF_NO_RST_SET && Signal=~*sync_q}
}

# ---------------------------------------------------------------------------
# INHERITED from arvern -- LANGUAGE_CHECK only, so gated on DO_LANG. Without
# -lang these rules never run and an ungated waiver would report stale every
# structural run.
# ---------------------------------------------------------------------------
if {[info exists DO_LANG] && $DO_LANG eq "1"} {

    # 64x64 product truncated to 64 bits (arv_alu_muldiv): RV32 MUL/MULH
    # sign/zero-extends the 32-bit operands to 64 and keeps the low 64 bits of
    # the product; the upper half carries no information. Five rules report the
    # single line, so they are waived together.
    # Gated: arv_alu_muldiv is not elaborated at M_EXTENSION=0.
    if {![info exists RTL_PARAM_M_EXTENSION] || $RTL_PARAM_M_EXTENSION != 0} {
        waive_hdl -add mul_product_truncation \
                  -comment "RV32 MUL/MULH: low 64 bits of the 64x64 product are the result by construction" \
                  -tag {CODING_WIDTH_UNEQ_SIZE CODING_WIDTH_UNEQ_SIG_ASSIGN CODING_EXPR_PRECISION_LOSS
                        SIMSYN_STMT_OPERAND_SIZE CODING_OPERAND_WIDTH}
    }

    # Verilog fixed-width arithmetic, in three idioms all verified against the
    # RTL: carry-drop (an N-bit +/- yields N+1 bits), shift-by-variable (a
    # 32-bit value shifted by a 5-bit amount reports Lhs=32 Rhs=5, and nothing
    # is truncated), and a ternary arm inheriting 33 bits from an increment.
    # Complying would mean explicit truncation slices on every adder in the
    # design, generating identical hardware.
    waive_hdl -add fixed_width_arithmetic \
              -comment "Verilog fixed-width arithmetic: carry-drop, shift-by-variable and ternary-arm width" \
              -tag {CONN_STMT_UNEQ_SIZE1 CODING_ASSIGN_UNEQUAL_LENGTH CODING_WIDTH_UNEQ_OPRND4}

    # arv_alu count_leading_zeros / count_trailing_zeros: the rule wants the
    # function-name assignment to be the LAST statement. Both assign it
    # unconditionally BEFORE the search loop, so the return value is always
    # defined; the rule objects to statement order.
    waive_hdl -add alu_count_fn_stmt_order \
              -comment "arv_alu CLZ/CTZ: return value assigned unconditionally before the loop, not last" \
              -tag CODING_FUNC_RET_STMT

    # JT FSM default branch (arv_uop_sequencer): the default is unreachable --
    # jt_state is 2 bits and all four states are enumerated -- and deliberately
    # so, keeping the block latch-free by construction. Removing it would fire
    # CODING_CASE_LATCHLIKE instead.
    # Gated: the JT state machine is built only for Zcmt (C_EXTENSION>=4).
    if {![info exists RTL_PARAM_C_EXTENSION] || $RTL_PARAM_C_EXTENSION >= 4} {
        waive_hdl -add jt_fsm_defensive_default \
                  -comment "JT FSM default is deliberately unreachable: defensive against undefined state" \
                  -tag CODING_CASE_DEFAULT_MSNG3 \
                  -filter {Module=~*uop_sequencer*}
    }
}

# ===========================================================================
# SoC-SPECIFIC
# ===========================================================================

# ---------------------------------------------------------------------------
# The behavioural memory macro stand-in.
#
# bench/verilog/sram_8kb_wrapper.v is not design RTL: the real wrapper is a
# foundry cell handed to synthesis through $SRAM_VERILOG_WRAPPER
# (synthesis/synopsys/library.tcl) and is deliberately absent from
# rtl/verilog/filelist.f. run_vclint substitutes the behavioural model so the
# memory ports stay connected and CONN_* checking over the SRAM controller
# interface remains meaningful.
#
# Waived by module scope rather than black-boxed, for exactly that reason: a
# black box would also blind the connectivity checks that are the point of
# including it. Findings inside the model are not findings about this design.
# ---------------------------------------------------------------------------
waive_hdl -add bench_memory_model \
          -comment "Behavioural stand-in for the foundry SRAM macro: not design RTL (synthesis reads \$SRAM_VERILOG_WRAPPER)" \
          -filter {Module=~sram_8kb_wrapper*}

# ---------------------------------------------------------------------------
# Registered feedback through a flop primitive.
#
# "Instance has input connected to output": a net that leaves an instance on
# q_o and re-enters it on d_i through an expression written inline in the port
# map. Every hit is a state-holding register written the ordinary way --
# toggle (.d_i(~req_level), .q_o(req_level)), shift ({data_phase[0],
# addr_phase}) -- plus the AHB hready feedback into the manager and subordinate
# muxes, which is the protocol. The loop is closed through a flop, never
# combinationally. The alternative is a named next-state wire per flop, which
# synthesises identically.
#
# Waived by tag rather than disabled in rules.tcl so the hits stay listed in
# report.lint_waived.txt. Both SoCs now carry this entry with the same
# population (DTM + interconnect + peripherals), which makes it a candidate
# for promotion to the topology block of rules.tcl.
# ---------------------------------------------------------------------------
waive_hdl -add ipdff_registered_feedback \
          -comment "q_o -> inline expression -> d_i on the same flop instance: registered feedback, not a combinational loop" \
          -tag CODING_INST_CONNECTED_INPUT_OUTPUT
