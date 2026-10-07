; ModuleID = '/Users/DEVELOPER/rtx-clear-air232-v1/compiled/input.air'
source_filename = "/Users/DEVELOPER/rtx-clear-air232-v1/source/clear231.metal"
target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v16:16:16-v24:32:32-v32:32:32-v48:64:64-v64:64:64-v96:128:128-v128:128:128-v192:256:256-v256:256:256-v512:512:512-v1024:1024:1024-n8:16:32"
target triple = "air64_v28-apple-macosx26.6.0"

%struct._texture_2d_t = type opaque

; Function Attrs: mustprogress nounwind willreturn
define void @rtx_clear_color231(%struct._texture_2d_t addrspace(1)* %0, float addrspace(1)* nocapture readonly "air-buffer-no-alias" %1, <2 x i32> %2) local_unnamed_addr #0 {
  %4 = load float, float addrspace(1)* %1, align 4, !tbaa !22, !alias.scope !26, !noalias !29
  %5 = insertelement <4 x float> undef, float %4, i64 0
  %6 = getelementptr inbounds float, float addrspace(1)* %1, i64 1
  %7 = load float, float addrspace(1)* %6, align 4, !tbaa !22, !alias.scope !26, !noalias !29
  %8 = insertelement <4 x float> %5, float %7, i64 1
  %9 = getelementptr inbounds float, float addrspace(1)* %1, i64 2
  %10 = load float, float addrspace(1)* %9, align 4, !tbaa !22, !alias.scope !26, !noalias !29
  %11 = insertelement <4 x float> %8, float %10, i64 2
  %12 = getelementptr inbounds float, float addrspace(1)* %1, i64 3
  %13 = load float, float addrspace(1)* %12, align 4, !tbaa !22, !alias.scope !26, !noalias !29
  %14 = insertelement <4 x float> %11, float %13, i64 3
  tail call void @air.write_texture_2d.v4f32(%struct._texture_2d_t addrspace(1)* nocapture %0, <2 x i32> %2, <4 x float> %14, i32 0, i32 2) #2, !alias.scope !29, !noalias !26
  ret void
}

; Function Attrs: argmemonly mustprogress nounwind willreturn
declare void @air.write_texture_2d.v4f32(%struct._texture_2d_t addrspace(1)* nocapture, <2 x i32>, <4 x float>, i32, i32) local_unnamed_addr #1

attributes #0 = { mustprogress nounwind willreturn "frame-pointer"="all" "min-legal-vector-width"="128" "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
attributes #1 = { argmemonly mustprogress nounwind willreturn }
attributes #2 = { argmemonly nounwind willreturn }

!llvm.module.flags = !{!0, !1, !2, !3, !4, !5, !6, !7, !8}
!air.kernel = !{!9}
!air.compile_options = !{!15, !16, !17}
!llvm.ident = !{!18}
!air.version = !{!19}
!air.language_version = !{!20}
!air.source_file_name = !{!21}

!0 = !{i32 2, !"SDK Version", [2 x i32] [i32 26, i32 5]}
!1 = !{i32 1, !"wchar_size", i32 4}
!2 = !{i32 7, !"frame-pointer", i32 2}
!3 = !{i32 7, !"air.max_device_buffers", i32 31}
!4 = !{i32 7, !"air.max_constant_buffers", i32 31}
!5 = !{i32 7, !"air.max_threadgroup_buffers", i32 31}
!6 = !{i32 7, !"air.max_textures", i32 128}
!7 = !{i32 7, !"air.max_read_write_textures", i32 8}
!8 = !{i32 7, !"air.max_samplers", i32 16}
!9 = !{void (%struct._texture_2d_t addrspace(1)*, float addrspace(1)*, <2 x i32>)* @rtx_clear_color231, !10, !11}
!10 = !{}
!11 = !{!12, !13, !14}
!12 = !{i32 0, !"air.texture", !"air.location_index", i32 0, i32 1, !"air.write", !"air.arg_type_name", !"texture2d<float, write>", !"air.arg_name", !"target"}
!13 = !{i32 1, !"air.buffer", !"air.location_index", i32 0, i32 1, !"air.read", !"air.address_space", i32 1, !"air.arg_type_size", i32 4, !"air.arg_type_align_size", i32 4, !"air.arg_type_name", !"float", !"air.arg_name", !"rgba"}
!14 = !{i32 2, !"air.thread_position_in_grid", !"air.arg_type_name", !"uint2", !"air.arg_name", !"position"}
!15 = !{!"air.compile.denorms_disable"}
!16 = !{!"air.compile.fast_math_disable"}
!17 = !{!"air.compile.framebuffer_fetch_enable"}
!18 = !{!"Apple metal version 32023.883 (metalfe-32023.883)"}
!19 = !{i32 2, i32 8, i32 0}
!20 = !{!"Metal", i32 2, i32 4, i32 0}
!21 = !{!"/Users/DEVELOPER/rtx-clear-air232-v1/source/clear231.metal"}
!22 = !{!23, !23, i64 0}
!23 = !{!"float", !24, i64 0}
!24 = !{!"omnipotent char", !25, i64 0}
!25 = !{!"Simple C++ TBAA"}
!26 = !{!27}
!27 = distinct !{!27, !28, !"air-alias-scope-arg(1)"}
!28 = distinct !{!28, !"air-alias-scopes(rtx_clear_color231)"}
!29 = !{!30}
!30 = distinct !{!30, !28, !"air-alias-scope-textures"}
