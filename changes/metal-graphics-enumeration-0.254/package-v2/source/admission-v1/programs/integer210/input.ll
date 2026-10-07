; ModuleID = '/private/var/root/rtx-livesource210-v1/normal/compiler/compiler-jobs/001/input.air'
source_filename = "runtime_210_same"
target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v16:16:16-v24:32:32-v32:32:32-v48:64:64-v64:64:64-v96:128:128-v128:128:128-v192:256:256-v256:256:256-v512:512:512-v1024:1024:1024-n8:16:32"
target triple = "air64_v28-apple-macosx26.6.0"

; Function Attrs: argmemonly mustprogress nofree norecurse nosync nounwind willreturn
define void @runtime_210_same(i32 addrspace(1)* nocapture readonly "air-buffer-no-alias" %a, i32 addrspace(1)* nocapture readonly "air-buffer-no-alias" %b, i32 addrspace(1)* nocapture writeonly "air-buffer-no-alias" %out, i32 %tid) local_unnamed_addr #0 {
entry:
  %idxprom = zext i32 %tid to i64
  %arrayidx = getelementptr inbounds i32, i32 addrspace(1)* %a, i64 %idxprom
  %0 = load i32, i32 addrspace(1)* %arrayidx, align 4, !tbaa !21, !alias.scope !25, !noalias !28
  %arrayidx2 = getelementptr inbounds i32, i32 addrspace(1)* %b, i64 %idxprom
  %1 = load i32, i32 addrspace(1)* %arrayidx2, align 4, !tbaa !21, !alias.scope !31, !noalias !32
  %add = add i32 %0, 13
  %add3 = add i32 %add, %1
  %arrayidx5 = getelementptr inbounds i32, i32 addrspace(1)* %out, i64 %idxprom
  store i32 %add3, i32 addrspace(1)* %arrayidx5, align 4, !tbaa !21, !alias.scope !33, !noalias !34
  ret void
}

attributes #0 = { argmemonly mustprogress nofree norecurse nosync nounwind willreturn "frame-pointer"="all" "min-legal-vector-width"="0" "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }

!llvm.module.flags = !{!0, !1, !2, !3, !4, !5, !6, !7}
!llvm.ident = !{!8}
!air.version = !{!9}
!air.language_version = !{!10}
!air.compile_options = !{!11, !12, !13}
!air.kernel = !{!14}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 7, !"frame-pointer", i32 2}
!2 = !{i32 7, !"air.max_device_buffers", i32 31}
!3 = !{i32 7, !"air.max_constant_buffers", i32 31}
!4 = !{i32 7, !"air.max_threadgroup_buffers", i32 31}
!5 = !{i32 7, !"air.max_textures", i32 128}
!6 = !{i32 7, !"air.max_read_write_textures", i32 8}
!7 = !{i32 7, !"air.max_samplers", i32 16}
!8 = !{!"Apple metal version 32023.886 (metalfe-32023.886.1)"}
!9 = !{i32 2, i32 8, i32 0}
!10 = !{!"Metal", i32 2, i32 4, i32 0}
!11 = !{!"air.compile.denorms_disable"}
!12 = !{!"air.compile.fast_math_disable"}
!13 = !{!"air.compile.framebuffer_fetch_enable"}
!14 = !{void (i32 addrspace(1)*, i32 addrspace(1)*, i32 addrspace(1)*, i32)* @runtime_210_same, !15, !16}
!15 = !{}
!16 = !{!17, !18, !19, !20}
!17 = !{i32 0, !"air.buffer", !"air.location_index", i32 0, i32 1, !"air.read", !"air.address_space", i32 1, !"air.arg_type_size", i32 4, !"air.arg_type_align_size", i32 4, !"air.arg_type_name", !"uint", !"air.arg_name", !"a"}
!18 = !{i32 1, !"air.buffer", !"air.location_index", i32 1, i32 1, !"air.read", !"air.address_space", i32 1, !"air.arg_type_size", i32 4, !"air.arg_type_align_size", i32 4, !"air.arg_type_name", !"uint", !"air.arg_name", !"b"}
!19 = !{i32 2, !"air.buffer", !"air.location_index", i32 2, i32 1, !"air.read_write", !"air.address_space", i32 1, !"air.arg_type_size", i32 4, !"air.arg_type_align_size", i32 4, !"air.arg_type_name", !"uint", !"air.arg_name", !"out"}
!20 = !{i32 3, !"air.thread_position_in_grid", !"air.arg_type_name", !"uint", !"air.arg_name", !"tid"}
!21 = !{!22, !22, i64 0}
!22 = !{!"int", !23, i64 0}
!23 = !{!"omnipotent char", !24, i64 0}
!24 = !{!"Simple C++ TBAA"}
!25 = !{!26}
!26 = distinct !{!26, !27, !"air-alias-scope-arg(0)"}
!27 = distinct !{!27, !"air-alias-scopes(runtime_210_same)"}
!28 = !{!29, !30}
!29 = distinct !{!29, !27, !"air-alias-scope-arg(1)"}
!30 = distinct !{!30, !27, !"air-alias-scope-arg(2)"}
!31 = !{!29}
!32 = !{!26, !30}
!33 = !{!30}
!34 = !{!26, !29}
