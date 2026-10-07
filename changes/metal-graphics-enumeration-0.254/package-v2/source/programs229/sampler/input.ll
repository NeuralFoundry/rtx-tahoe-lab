; ModuleID = '/Users/DEVELOPER/rtx-sampler-air227-v1/compiled/input.air'
source_filename = "/Users/DEVELOPER/rtx-sampler-air227-v1/source/sampler227.metal"
target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v16:16:16-v24:32:32-v32:32:32-v48:64:64-v64:64:64-v96:128:128-v128:128:128-v192:256:256-v256:256:256-v512:512:512-v1024:1024:1024-n8:16:32"
target triple = "air64_v28-apple-macosx26.6.0"

%struct._texture_2d_t = type opaque
%struct._sampler_t = type opaque

; Function Attrs: convergent mustprogress nofree nounwind willreturn
define void @sample_texture227(%struct._texture_2d_t addrspace(1)* %0, %struct._sampler_t addrspace(2)* nocapture readonly %1, float addrspace(1)* nocapture readonly "air-buffer-no-alias" %2, float addrspace(1)* nocapture writeonly "air-buffer-no-alias" %3, i32 %4) local_unnamed_addr #0 {
  %6 = shl i32 %4, 1
  %7 = zext i32 %6 to i64
  %8 = getelementptr inbounds float, float addrspace(1)* %2, i64 %7
  %9 = load float, float addrspace(1)* %8, align 4, !tbaa !24, !alias.scope !28, !noalias !31
  %10 = insertelement <2 x float> undef, float %9, i64 0
  %11 = or i32 %6, 1
  %12 = zext i32 %11 to i64
  %13 = getelementptr inbounds float, float addrspace(1)* %2, i64 %12
  %14 = load float, float addrspace(1)* %13, align 4, !tbaa !24, !alias.scope !28, !noalias !31
  %15 = insertelement <2 x float> %10, float %14, i64 1
  %16 = tail call { <4 x float>, i8 } @air.sample_texture_2d.v4f32(%struct._texture_2d_t addrspace(1)* nocapture readonly %0, %struct._sampler_t addrspace(2)* nocapture readonly %1, <2 x float> %15, i1 true, <2 x i32> zeroinitializer, i1 true, float 0.000000e+00, float 0.000000e+00, i32 0) #2, !alias.scope !35, !noalias !36
  %17 = extractvalue { <4 x float>, i8 } %16, 0
  %18 = extractelement <4 x float> %17, i64 0
  %19 = shl i32 %4, 2
  %20 = zext i32 %19 to i64
  %21 = getelementptr inbounds float, float addrspace(1)* %3, i64 %20
  store float %18, float addrspace(1)* %21, align 4, !tbaa !24, !alias.scope !37, !noalias !38
  %22 = extractelement <4 x float> %17, i64 1
  %23 = or i32 %19, 1
  %24 = zext i32 %23 to i64
  %25 = getelementptr inbounds float, float addrspace(1)* %3, i64 %24
  store float %22, float addrspace(1)* %25, align 4, !tbaa !24, !alias.scope !37, !noalias !38
  %26 = extractelement <4 x float> %17, i64 2
  %27 = or i32 %19, 2
  %28 = zext i32 %27 to i64
  %29 = getelementptr inbounds float, float addrspace(1)* %3, i64 %28
  store float %26, float addrspace(1)* %29, align 4, !tbaa !24, !alias.scope !37, !noalias !38
  %30 = extractelement <4 x float> %17, i64 3
  %31 = or i32 %19, 3
  %32 = zext i32 %31 to i64
  %33 = getelementptr inbounds float, float addrspace(1)* %3, i64 %32
  store float %30, float addrspace(1)* %33, align 4, !tbaa !24, !alias.scope !37, !noalias !38
  ret void
}

; Function Attrs: argmemonly convergent mustprogress nofree nounwind readonly willreturn
declare { <4 x float>, i8 } @air.sample_texture_2d.v4f32(%struct._texture_2d_t addrspace(1)* nocapture readonly, %struct._sampler_t addrspace(2)* nocapture readonly, <2 x float>, i1, <2 x i32>, i1, float, float, i32) local_unnamed_addr #1

attributes #0 = { convergent mustprogress nofree nounwind willreturn "frame-pointer"="all" "min-legal-vector-width"="128" "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
attributes #1 = { argmemonly convergent mustprogress nofree nounwind readonly willreturn }
attributes #2 = { argmemonly convergent nounwind readonly willreturn }

!llvm.module.flags = !{!0, !1, !2, !3, !4, !5, !6, !7, !8}
!air.kernel = !{!9}
!air.compile_options = !{!17, !18, !19}
!llvm.ident = !{!20}
!air.version = !{!21}
!air.language_version = !{!22}
!air.source_file_name = !{!23}

!0 = !{i32 2, !"SDK Version", [2 x i32] [i32 26, i32 5]}
!1 = !{i32 1, !"wchar_size", i32 4}
!2 = !{i32 7, !"frame-pointer", i32 2}
!3 = !{i32 7, !"air.max_device_buffers", i32 31}
!4 = !{i32 7, !"air.max_constant_buffers", i32 31}
!5 = !{i32 7, !"air.max_threadgroup_buffers", i32 31}
!6 = !{i32 7, !"air.max_textures", i32 128}
!7 = !{i32 7, !"air.max_read_write_textures", i32 8}
!8 = !{i32 7, !"air.max_samplers", i32 16}
!9 = !{void (%struct._texture_2d_t addrspace(1)*, %struct._sampler_t addrspace(2)*, float addrspace(1)*, float addrspace(1)*, i32)* @sample_texture227, !10, !11}
!10 = !{}
!11 = !{!12, !13, !14, !15, !16}
!12 = !{i32 0, !"air.texture", !"air.location_index", i32 0, i32 1, !"air.sample", !"air.arg_type_name", !"texture2d<float, sample>", !"air.arg_name", !"source"}
!13 = !{i32 1, !"air.sampler", !"air.location_index", i32 0, i32 1, !"air.arg_type_name", !"sampler", !"air.arg_name", !"sampling"}
!14 = !{i32 2, !"air.buffer", !"air.location_index", i32 0, i32 1, !"air.read", !"air.address_space", i32 1, !"air.arg_type_size", i32 4, !"air.arg_type_align_size", i32 4, !"air.arg_type_name", !"float", !"air.arg_name", !"uv"}
!15 = !{i32 3, !"air.buffer", !"air.location_index", i32 1, i32 1, !"air.read_write", !"air.address_space", i32 1, !"air.arg_type_size", i32 4, !"air.arg_type_align_size", i32 4, !"air.arg_type_name", !"float", !"air.arg_name", !"output"}
!16 = !{i32 4, !"air.thread_position_in_grid", !"air.arg_type_name", !"uint", !"air.arg_name", !"gid"}
!17 = !{!"air.compile.denorms_disable"}
!18 = !{!"air.compile.fast_math_disable"}
!19 = !{!"air.compile.framebuffer_fetch_enable"}
!20 = !{!"Apple metal version 32023.883 (metalfe-32023.883)"}
!21 = !{i32 2, i32 8, i32 0}
!22 = !{!"Metal", i32 2, i32 4, i32 0}
!23 = !{!"/Users/DEVELOPER/rtx-sampler-air227-v1/source/sampler227.metal"}
!24 = !{!25, !25, i64 0}
!25 = !{!"float", !26, i64 0}
!26 = !{!"omnipotent char", !27, i64 0}
!27 = !{!"Simple C++ TBAA"}
!28 = !{!29}
!29 = distinct !{!29, !30, !"air-alias-scope-arg(2)"}
!30 = distinct !{!30, !"air-alias-scopes(sample_texture227)"}
!31 = !{!32, !33, !34}
!32 = distinct !{!32, !30, !"air-alias-scope-textures"}
!33 = distinct !{!33, !30, !"air-alias-scope-samplers"}
!34 = distinct !{!34, !30, !"air-alias-scope-arg(3)"}
!35 = !{!32, !33}
!36 = !{!29, !34}
!37 = !{!34}
!38 = !{!32, !33, !29}
