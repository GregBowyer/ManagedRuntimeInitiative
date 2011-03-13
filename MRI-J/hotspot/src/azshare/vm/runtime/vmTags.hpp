// Copyright 2010 Azul Systems, Inc.  All Rights Reserved.
// DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
//
// This code is free software; you can redistribute it and/or modify it under 
// the terms of the GNU General Public License version 2 only, as published by 
// the Free Software Foundation. 
//
// This code is distributed in the hope that it will be useful, but WITHOUT ANY 
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
// A PARTICULAR PURPOSE.  See the GNU General Public License version 2 for  more
// details (a copy is included in the LICENSE file that accompanied this code).
//
// You should have received a copy of the GNU General Public License version 2 
// along with this work; if not, write to the Free Software Foundation,Inc., 
// 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
// 
// Please contact Azul Systems, Inc., 1600 Plymouth Street, Mountain View, 
// CA 94043 USA, or visit www.azulsystems.com if you need additional information 
// or have any questions.
#ifndef VMTAGS_HPP
#define VMTAGS_HPP


#include "allocation.hpp"


#define VMOPS_TAGS_DO(template) \
  template(VMOp_unclassified) \
  template(VMOp_VMThreadSafepoint) \
  template(VMOp_GC_Operation) \
  template(VMOp_GenCollectFull) \
  template(VMOp_GenCollectForAllocation) \
  template(VMOp_ParallelGCFailedAllocation) \
  template(VMOp_ParallelGCFailedPermanentAllocation) \
  template(VMOp_ParallelGCRequestedGC) \
  template(VMOp_ForceSafepoint) \
  template(VMOp_Deoptimize) \
  template(VMOp_Verify) \
  template(VMOp_PrintThreads) \
  template(VMOp_FindDeadlocks) \
  template(VMOp_GC_HeapInspection) \
  template(VMOp_Exit)

#define VM_TAGS_DO(template) \
  template(VM_Startup) \
  template(VM_ThreadStart) \
  template(VM_Interpreter) \
  template(VM_JavaCode) \
  template(VM_CompilerInterface) \
  template(VM_C1Compiler) \
  template(VM_C2Compiler) \
  template(VM_GCTask) \
  template(VM_NewGPGC) \
  template(VM_OldGPGC) \
  template(VM_PGC) \
  template(VM_REALTime_Performance_Monitor)

#define JNI_TAGS_DO(template) \
  template(jni_DefineClass) \
  template(jni_FindClass) \
  template(jni_FromReflectedMethod) \
  template(jni_FromReflectedField) \
  template(jni_ToReflectedMethod) \
  template(jni_GetSuperclass) \
  template(jni_Throw) \
  template(jni_ThrowNew) \
  template(jni_ExceptionOccurred) \
  template(jni_ExceptionDescribe) \
  template(jni_FatalError) \
  template(jni_PushLocalFrame) \
  template(jni_PopLocalFrame) \
  template(jni_NewGlobalRef) \
  template(jni_DeleteGlobalRef) \
  template(jni_NewLocalRef) \
  template(jni_AllocObject) \
  template(jni_NewObjectA) \
  template(jni_NewObjectV) \
  template(jni_NewObject) \
  template(jni_GetObjectClass) \
  template(jni_GetMethodID) \
  template(jni_GetStaticMethodID) \
  template(jni_CallVoidMethod) \
  template(jni_CallVoidMethodV) \
  template(jni_CallVoidMethodA) \
  template(jni_CallNonvirtualVoidMethod) \
  template(jni_CallNonvirtualVoidMethodV) \
  template(jni_CallNonvirtualVoidMethodA) \
  template(jni_CallStaticVoidMethod) \
  template(jni_CallStaticVoidMethodV) \
  template(jni_CallStaticVoidMethodA) \
  template(jni_GetFieldID) \
  template(jni_GetObjectField) \
  template(jni_ToReflectedField) \
  template(jni_GetStaticFieldID) \
  template(jni_GetStaticObjectField) \
  template(jni_SetStaticObjectField) \
  template(jni_NewString) \
  template(jni_NewStringUTF) \
  template(jni_GetStringUTFLength) \
  template(jni_GetStringUTFChars) \
  template(jni_NewObjectArray) \
  template(jni_GetObjectArrayElement) \
  template(jni_SetObjectArrayElement) \
  template(jni_RegisterNatives) \
  template(jni_UnregisterNatives) \
  template(jni_MonitorEnter) \
  template(jni_MonitorExit) \
  template(jni_GetStringRegion) \
  template(jni_GetStringUTFRegion) \
  template(jni_GetPrimitiveArrayTypeReal) \
  template(jni_GetPrimitiveArrayCritical) \
  template(jni_ReleasePrimitiveArrayCritical) \
  template(jni_GetStringCritical) \
  template(jni_ReleaseStringCritical) \
  template(jni_NewWeakGlobalRef) \
  template(jni_DeleteWeakGlobalRef) \
  template(lookupDirectBufferClasses) \
  template(throw_unsatisfied_link_error) \
  template(jni_IsAssignableFrom) \
  template(jni_ExceptionClear) \
  template(jni_DeleteLocalRef) \
  template(jni_IsSameObject) \
  template(jni_IsInstanceOf) \
  template(jni_SetObjectField) \
  template(jni_GetStringLength) \
  template(jni_GetStringChars) \
  template(jni_ReleaseStringChars) \
  template(jni_GetArrayLength) \
  template(jni_ExceptionCheck) \
  template(jni_EnsureLocalCapacity) \
  template(jni_GetObjectRefType) \
  template(jni_ReleaseStringUTFChars) \
  template(jni_GetVersion) \
  template(jni_GetJavaVM) \
  template(jni_CallBooleanMethod) \
  template(jni_CallBooleanMethodV) \
  template(jni_CallBooleanMethodA) \
  template(jni_CallByteMethod) \
  template(jni_CallByteMethodV) \
  template(jni_CallByteMethodA) \
  template(jni_CallCharMethod) \
  template(jni_CallCharMethodV) \
  template(jni_CallCharMethodA) \
  template(jni_CallShortMethod) \
  template(jni_CallShortMethodV) \
  template(jni_CallShortMethodA) \
  template(jni_CallObjectMethod) \
  template(jni_CallObjectMethodV) \
  template(jni_CallObjectMethodA) \
  template(jni_CallIntMethod) \
  template(jni_CallIntMethodV) \
  template(jni_CallIntMethodA) \
  template(jni_CallLongMethod) \
  template(jni_CallLongMethodV) \
  template(jni_CallLongMethodA) \
  template(jni_CallFloatMethod) \
  template(jni_CallFloatMethodV) \
  template(jni_CallFloatMethodA) \
  template(jni_CallDoubleMethod) \
  template(jni_CallDoubleMethodV) \
  template(jni_CallDoubleMethodA) \
  template(jni_CallNonvirtualBooleanMethod) \
  template(jni_CallNonvirtualBooleanMethodV) \
  template(jni_CallNonvirtualBooleanMethodA) \
  template(jni_CallNonvirtualByteMethod) \
  template(jni_CallNonvirtualByteMethodV) \
  template(jni_CallNonvirtualByteMethodA) \
  template(jni_CallNonvirtualCharMethod) \
  template(jni_CallNonvirtualCharMethodV) \
  template(jni_CallNonvirtualCharMethodA) \
  template(jni_CallNonvirtualShortMethod) \
  template(jni_CallNonvirtualShortMethodV) \
  template(jni_CallNonvirtualShortMethodA) \
  template(jni_CallNonvirtualObjectMethod) \
  template(jni_CallNonvirtualObjectMethodV) \
  template(jni_CallNonvirtualObjectMethodA) \
  template(jni_CallNonvirtualIntMethod) \
  template(jni_CallNonvirtualIntMethodV) \
  template(jni_CallNonvirtualIntMethodA) \
  template(jni_CallNonvirtualLongMethod) \
  template(jni_CallNonvirtualLongMethodV) \
  template(jni_CallNonvirtualLongMethodA) \
  template(jni_CallNonvirtualFloatMethod) \
  template(jni_CallNonvirtualFloatMethodV) \
  template(jni_CallNonvirtualFloatMethodA) \
  template(jni_CallNonvirtualDoubleMethod) \
  template(jni_CallNonvirtualDoubleMethodV) \
  template(jni_CallNonvirtualDoubleMethodA) \
  template(jni_CallStaticBooleanMethod) \
  template(jni_CallStaticBooleanMethodV) \
  template(jni_CallStaticBooleanMethodA) \
  template(jni_CallStaticByteMethod) \
  template(jni_CallStaticByteMethodV) \
  template(jni_CallStaticByteMethodA) \
  template(jni_CallStaticCharMethod) \
  template(jni_CallStaticCharMethodV) \
  template(jni_CallStaticCharMethodA) \
  template(jni_CallStaticShortMethod) \
  template(jni_CallStaticShortMethodV) \
  template(jni_CallStaticShortMethodA) \
  template(jni_CallStaticObjectMethod) \
  template(jni_CallStaticObjectMethodV) \
  template(jni_CallStaticObjectMethodA) \
  template(jni_CallStaticIntMethod) \
  template(jni_CallStaticIntMethodV) \
  template(jni_CallStaticIntMethodA) \
  template(jni_CallStaticLongMethod) \
  template(jni_CallStaticLongMethodV) \
  template(jni_CallStaticLongMethodA) \
  template(jni_CallStaticFloatMethod) \
  template(jni_CallStaticFloatMethodV) \
  template(jni_CallStaticFloatMethodA) \
  template(jni_CallStaticDoubleMethod) \
  template(jni_CallStaticDoubleMethodV) \
  template(jni_CallStaticDoubleMethodA) \
  template(jni_GetBooleanField) \
  template(jni_GetByteField) \
  template(jni_GetCharField) \
  template(jni_GetShortField) \
  template(jni_GetIntField) \
  template(jni_GetLongField) \
  template(jni_GetFloatField) \
  template(jni_GetDoubleField) \
  template(jni_SetBooleanField) \
  template(jni_SetByteField) \
  template(jni_SetCharField) \
  template(jni_SetShortField) \
  template(jni_SetIntField) \
  template(jni_SetLongField) \
  template(jni_SetFloatField) \
  template(jni_SetDoubleField) \
  template(jni_GetStaticBooleanField) \
  template(jni_GetStaticByteField) \
  template(jni_GetStaticCharField) \
  template(jni_GetStaticShortField) \
  template(jni_GetStaticIntField) \
  template(jni_GetStaticLongField) \
  template(jni_GetStaticFloatField) \
  template(jni_GetStaticDoubleField) \
  template(jni_SetStaticBooleanField) \
  template(jni_SetStaticByteField) \
  template(jni_SetStaticCharField) \
  template(jni_SetStaticShortField) \
  template(jni_SetStaticIntField) \
  template(jni_SetStaticLongField) \
  template(jni_SetStaticFloatField) \
  template(jni_SetStaticDoubleField) \
  template(jni_NewBooleanArray) \
  template(jni_NewByteArray) \
  template(jni_NewShortArray) \
  template(jni_NewCharArray) \
  template(jni_NewIntArray) \
  template(jni_NewLongArray) \
  template(jni_NewFloatArray) \
  template(jni_NewDoubleArray) \
  template(jni_GetBooleanArrayElements) \
  template(jni_GetByteArrayElements) \
  template(jni_GetShortArrayElements) \
  template(jni_GetCharArrayElements) \
  template(jni_GetIntArrayElements) \
  template(jni_GetLongArrayElements) \
  template(jni_GetFloatArrayElements) \
  template(jni_GetDoubleArrayElements) \
  template(jni_ReleaseBooleanArrayElements) \
  template(jni_ReleaseByteArrayElements) \
  template(jni_ReleaseShortArrayElements) \
  template(jni_ReleaseCharArrayElements) \
  template(jni_ReleaseIntArrayElements) \
  template(jni_ReleaseLongArrayElements) \
  template(jni_ReleaseFloatArrayElements) \
  template(jni_ReleaseDoubleArrayElements) \
  template(jni_GetBooleanArrayRegion) \
  template(jni_GetByteArrayRegion) \
  template(jni_GetShortArrayRegion) \
  template(jni_GetCharArrayRegion) \
  template(jni_GetIntArrayRegion) \
  template(jni_GetLongArrayRegion) \
  template(jni_GetFloatArrayRegion) \
  template(jni_GetDoubleArrayRegion) \
  template(jni_SetBooleanArrayRegion) \
  template(jni_SetByteArrayRegion) \
  template(jni_SetShortArrayRegion) \
  template(jni_SetCharArrayRegion) \
  template(jni_SetIntArrayRegion) \
  template(jni_SetLongArrayRegion) \
  template(jni_SetFloatArrayRegion) \
  template(jni_SetDoubleArrayRegion) \
  template(jni_RegisterRemoteNativesReal)


#define JVM_TAGS_DO(template) \
  template(JVM_RegisterSignal) \
  template(JVM_RaiseSignal) \
  template(JVM_FindSignal) \
  template(JVM_RegisterPerfMethods) \
  template(JVM_RegisterUnsafeMethods) \
  template(JVM_ArrayCopy) \
  template(JVM_InitProperties) \
  template(JVM_Exit) \
  template(JVM_Halt) \
  template(JVM_GC) \
  template(JVM_SystemResourceLimit_GC) \
  template(JVM_TotalMemory) \
  template(JVM_FreeMemory) \
  template(JVM_MaxMemory) \
  template(JVM_ActiveProcessorCount) \
  template(JVM_FillInStackTrace) \
  template(JVM_PrintStackTrace) \
  template(JVM_GetStackTraceDepth) \
  template(JVM_GetStackTraceElement) \
  template(JVM_IHashCode) \
  template(JVM_MonitorWait) \
  template(JVM_MonitorNotify) \
  template(JVM_MonitorNotifyAll) \
  template(JVM_Clone) \
  template(JVM_ShallowCopy) \
  template(JVM_ReferenceEquality) \
  template(JVM_GetCallerClass) \
  template(JVM_FindPrimitiveClass) \
  template(JVM_ResolveClass) \
  template(JVM_FindClassFromClassLoader) \
  template(JVM_FindClassFromClass) \
  template(JVM_DefineClass) \
  template(JVM_DefineClassWithSource) \
  template(JVM_FindLoadedClass) \
  template(JVM_GetClassName) \
  template(JVM_GetClassInterfaces) \
  template(JVM_GetClassLoader) \
  template(JVM_GetClassSigners) \
  template(JVM_SetClassSigners) \
  template(JVM_GetProtectionDomain) \
  template(JVM_SetProtectionDomain) \
  template(JVM_DoPrivileged) \
  template(JVM_GetInheritedAccessControlContext) \
  template(JVM_GetStackAccessControlContext) \
  template(JVM_GetComponentType) \
  template(JVM_GetClassModifiers) \
  template(JVM_GetDeclaredClasses) \
  template(JVM_GetDeclaringClass) \
  template(JVM_GetClassSignature) \
  template(JVM_GetClassAnnotations) \
  template(JVM_GetFieldAnnotations) \
  template(JVM_GetMethodAnnotations) \
  template(JVM_GetMethodDefaultAnnotationValue) \
  template(JVM_GetMethodParameterAnnotations) \
  template(JVM_GetClassDeclaredFields) \
  template(JVM_GetClassDeclaredMethods) \
  template(JVM_GetClassDeclaredConstructors) \
  template(JVM_GetClassAccessFlags) \
  template(JVM_GetClassConstantPool) \
  template(JVM_ConstantPoolGetSize) \
  template(JVM_ConstantPoolGetClassAt) \
  template(JVM_ConstantPoolGetClassAtIfLoaded) \
  template(JVM_ConstantPoolGetMethodAt) \
  template(JVM_ConstantPoolGetMethodAtIfLoaded) \
  template(JVM_ConstantPoolGetFieldAt) \
  template(JVM_ConstantPoolGetFieldAtIfLoaded) \
  template(JVM_ConstantPoolGetMemberRefInfoAt) \
  template(JVM_ConstantPoolGetIntAt) \
  template(JVM_ConstantPoolGetLongAt) \
  template(JVM_ConstantPoolGetFloatAt) \
  template(JVM_ConstantPoolGetDoubleAt) \
  template(JVM_ConstantPoolGetStringAt) \
  template(JVM_ConstantPoolGetUTF8At) \
  template(JVM_DesiredAssertionStatus) \
  template(JVM_AssertionStatusDirectives) \
  template(JVM_GetClassNameUTF) \
  template(JVM_GetMethodIxNameUTF) \
  template(JVM_GetMethodIxSignatureUTF) \
  template(JVM_GetCPFieldNameUTF) \
  template(JVM_GetCPMethodNameUTF) \
  template(JVM_GetCPMethodSignatureUTF) \
  template(JVM_GetCPFieldSignatureUTF) \
  template(JVM_GetCPClassNameUTF) \
  template(JVM_GetCPFieldClassNameUTF) \
  template(JVM_GetCPMethodClassNameUTF) \
  template(JVM_IsSameClassPackage) \
  template(JVM_StartThread) \
  template(JVM_StopThread) \
  template(JVM_IsThreadAlive) \
  template(JVM_SuspendThread) \
  template(JVM_ResumeThread) \
  template(JVM_SetThreadPriority) \
  template(JVM_Yield) \
  template(JVM_Sleep) \
  template(JVM_CurrentThread) \
  template(JVM_CountStackFrames) \
  template(JVM_Interrupt) \
  template(JVM_HoldsLock) \
  template(JVM_DumpAllStacks) \
  template(JVM_CurrentLoadedClass) \
  template(JVM_CurrentClassLoader) \
  template(JVM_GetClassContext) \
  template(JVM_ClassDepth) \
  template(JVM_ClassLoaderDepth) \
  template(JVM_GetSystemPackage) \
  template(JVM_GetSystemPackages) \
  template(JVM_AllocateNewObject) \
  template(JVM_AllocateNewArray) \
  template(JVM_LatestUserDefinedLoader) \
  template(JVM_LoadClass0) \
  template(JVM_GetArrayLength) \
  template(JVM_GetArrayElement) \
  template(JVM_GetPrimitiveArrayElement) \
  template(JVM_SetArrayElement) \
  template(JVM_SetPrimitiveArrayElement) \
  template(JVM_NewArray) \
  template(JVM_NewMultiArray) \
  template(JVM_LoadLibrary) \
  template(JVM_InternString) \
  template(JVM_SetPrimitiveFieldValues) \
  template(JVM_GetPrimitiveFieldValues) \
  template(JVM_VMBreakPoint) \
  template(JVM_GetClassFields) \
  template(JVM_GetClassMethods) \
  template(JVM_GetClassConstructors) \
  template(JVM_GetClassField) \
  template(JVM_GetClassMethod) \
  template(JVM_GetClassConstructor) \
  template(JVM_NewInstance) \
  template(JVM_GetField) \
  template(JVM_GetPrimitiveField) \
  template(JVM_SetField) \
  template(JVM_SetPrimitiveField) \
  template(JVM_InvokeMethod) \
  template(JVM_NewInstanceFromConstructor) \
  template(JVM_CX8Field) \
  template(JVM_GetAllThreads) \
  template(JVM_DumpThreads) \
  template(JVM_GetManagement) \
  template(JVM_InitAgentProperties) \
  template(JVM_GetEnclosingMethodInfo) \
  template(JVM_GetThreadStateValues) \
  template(JVM_GetThreadStateNames) \
  template(JVM_DumpHeap) \
  template(JVM_GetVersionInfo) \
  template(JVM_MetaTick) \
  template(JVM_IsInterface) \
  template(JVM_IsArrayClass) \
  template(JVM_IsPrimitiveClass) \
  template(JVM_GetClassCPTypes) \
  template(JVM_GetClassCPEntriesCount) \
  template(JVM_GetClassFieldsCount) \
  template(JVM_GetClassMethodsCount) \
  template(JVM_GetMethodIxExceptionIndexes) \
  template(JVM_GetMethodIxExceptionsCount) \
  template(JVM_GetMethodIxByteCode) \
  template(JVM_GetMethodIxByteCodeLength) \
  template(JVM_GetMethodIxExceptionTableEntry) \
  template(JVM_GetMethodIxExceptionTableLength) \
  template(JVM_GetMethodIxModifiers) \
  template(JVM_GetFieldIxModifiers) \
  template(JVM_GetMethodIxLocalsCount) \
  template(JVM_GetMethodIxArgsSize) \
  template(JVM_GetMethodIxMaxStack) \
  template(JVM_IsConstructorIx) \
  template(JVM_GetCPFieldModifiers) \
  template(JVM_GetCPMethodModifiers) \
  template(JVM_IsInterrupted) \
  template(JVM_GetInterfaceVersion) \
  template(JVM_CurrentTimeMillis) \
  template(JVM_NanoTime) \
  template(JVM_OnExit) \
  template(JVM_MaxObjectInspectionAge) \
  template(JVM_TraceInstructions) \
  template(JVM_TraceMethodCalls) \
  template(JVM_InitializeCompiler) \
  template(JVM_IsSilentCompiler) \
  template(JVM_CompileClass) \
  template(JVM_CompileClasses) \
  template(JVM_CompilerCommand) \
  template(JVM_EnableCompiler) \
  template(JVM_DisableCompiler) \
  template(JVM_GetLastErrorString) \
  template(JVM_NativePath) \
  template(JVM_ReleaseUTF) \
  template(JVM_Open) \
  template(JVM_Close) \
  template(JVM_Read) \
  template(JVM_Write) \
  template(JVM_Available) \
  template(JVM_Lseek) \
  template(JVM_SetLength) \
  template(JVM_Sync) \
  template(JVM_InitializeSocketLibrary) \
  template(JVM_Socket) \
  template(JVM_SocketClose) \
  template(JVM_SocketShutdown) \
  template(JVM_Recv) \
  template(JVM_Send) \
  template(JVM_Timeout) \
  template(JVM_Listen) \
  template(JVM_Connect) \
  template(JVM_Bind) \
  template(JVM_Accept) \
  template(JVM_RecvFrom) \
  template(JVM_GetSockName) \
  template(JVM_SendTo) \
  template(JVM_SocketAvailable) \
  template(JVM_GetSockOpt) \
  template(JVM_SetSockOpt) \
  template(JVM_GetHostName) \
  template(JVM_GetHostByAddr) \
  template(JVM_GetHostByName) \
  template(JVM_GetProtoByName) \
  template(JVM_UnloadLibrary) \
  template(JVM_FindLibraryEntry) \
  template(JVM_IsNaN) \
  template(JVM_IsSupportedJNIVersion) \
  template(JVM_AccessVMBooleanFlag) \
  template(JVM_AccessVMIntFlag) \
  template(JVM_SupportsCX8) \
  template(JVM_Avm2ProxyJNIEnv) \
  template(JVM_GetMethodSignature) \
  template(JVM_GetMethodSignatureFromUnknownThread) \
  template(JVM_GetThreadId) \
  template(JVM_GetTagAndThreadId) \
  template(JVM_SetCallbackWrapperTag) \
  template(JVM_SetHeapIterationControl) \
  template(JVM_ShouldUseSynchronousHeapCallbacks) \
  template(JVM_GetMethodReturnType) \
  template(JVM_GetUseDebugLibrarySuffix) \
  template(JVM_GetUseLockedCollections) \
  template(JVM_GetUseCheckedCollections) \
  template(JVM_BlockingHint_set) \
  template(JVM_GetJavaThreadLocalMapInitialCapacity) \
  template(JVM_TicksToNanos) \
  template(JVM_CollectWallTicks) \
  template(JVM_CollectCpuTicks) \
  template(JVM_CollectBlockedTicks) \
  template(JVM_CollectObjWaitTicks) \
  template(JVM_CollectNetWaitTicks) \
  template(JVM_CollectFileWaitTicks) \
  template(JVM_CollectGCWaitTicks) \
  template(JVM_CollectSafepointWaitTicks) \
  template(JVM_CollectCpuWaitTicks) \
  template(JVM_ParkTLAB) \
  template(JVM_UnparkTLAB) \
  template(JVM_GetFileDescriptorFields) \
  template(JVM_ReadByteArrayRegion) \
  template(JVM_WriteByteArrayRegion) \
  template(JVM_InitFileDescriptorField) \
  template(JVM_IsFileDescriptorField) \
  template(JVM_IsJVMPIEnabled) \
  template(JVM_ExternallyDisableJVMPIObjectAllocEvent)  \
  template(JVM_GetObjectArrayRegionIntField)  \
  template(JVM_SetObjectArrayRegionIntField)  \
  template(JVM_GetObjectFields) \
  template(JVM_CachejfieldID) \
  template(JVM_SetFinalizerThread) \
  template(JVM_ReferenceGetReferent) \
  template(JVM_ReferencePendingInNext) \
  template(JVM_DatagramSendTo) \
  template(JVM_DatagramRecvFrom) \
  template(JVM_DatagramRecvFromSet) \
  template(JVM_GetSystemThreadGroup) \
  template(Java_weblogic_socket_PosixSocketMuxer_initStripes) \
  template(Java_weblogic_socket_PosixSocketMuxer_pollStripe) \
  template(Java_weblogic_socket_PosixSocketMuxer_wakeupStripe) \
  template(Java_weblogic_socket_PosixSocketMuxer_getSoftFdLimit) \
  template(Java_weblogic_socket_PosixSocketMuxer_getHardFdLimit) \
  template(Java_weblogic_socket_PosixSocketMuxer_getCurrentFdLimit) \
  template(Java_weblogic_socket_PosixSocketMuxer_getBuildTime) \
  template(Java_weblogic_socket_PosixSocketMuxer_setDebug) \
                                     \
  template(jmm_GetVersion) \
  template(jmm_GetOptionalSupport) \
  template(jmm_GetInputArguments) \
  template(jmm_GetInputArgumentArray) \
  template(jmm_GetMemoryPools) \
  template(jmm_GetMemoryManagers) \
  template(jmm_GetMemoryPoolUsage) \
  template(jmm_GetPeakMemoryPoolUsage) \
  template(jmm_GetPoolCollectionUsage) \
  template(jmm_SetPoolSensor) \
  template(jmm_SetPoolThreshold) \
  template(jmm_GetMemoryUsage) \
  template(jmm_GetBoolAttribute) \
  template(jmm_SetBoolAttribute) \
  template(jmm_GetLongAttribute) \
  template(jmm_GetLongAttributes) \
  template(jmm_GetThreadInfo) \
  template(jmm_DumpThreads) \
  template(jmm_GetLoadedClasses) \
  template(jmm_ResetStatistic) \
  template(jmm_GetThreadCpuTime) \
  template(jmm_GetThreadCpuTimeWithKind) \
  template(jmm_GetVMGlobalNames) \
  template(jmm_GetVMGlobals) \
  template(jmm_SetVMGlobal) \
  template(jmm_GetInternalThreadTimes) \
  template(jmm_FindDeadlockedThreads) \
  template(jmm_FindMonitorDeadlockedThreads) \
  template(jmm_FindCircularBlockedThreads) \
  template(jmm_GetGCExtAttributeInfo) \
  template(jmm_GetLastGCStat) \
  template(jmm_DumpHeap) \
  template(jmm_DumpHeap0)

#define UNSAFE_TAGS_DO(template) \
  template(Unsafe_GetObject140) \
  template(Unsafe_SetObject140) \
  template(Unsafe_GetObject) \
  template(Unsafe_SetObject) \
  template(Unsafe_GetObjectVolatile) \
  template(Unsafe_SetObjectVolatile) \
  template(Unsafe_GetLongVolatile) \
  template(Unsafe_SetLongVolatile) \
  template(Unsafe_GetBooleanVolatile) \
  template(Unsafe_SetBooleanVolatile) \
  template(Unsafe_GetByteVolatile) \
  template(Unsafe_SetByteVolatile) \
  template(Unsafe_GetShortVolatile) \
  template(Unsafe_SetShortVolatile) \
  template(Unsafe_GetCharVolatile) \
  template(Unsafe_SetCharVolatile) \
  template(Unsafe_GetIntVolatile) \
  template(Unsafe_SetIntVolatile) \
  template(Unsafe_GetFloatVolatile) \
  template(Unsafe_SetFloatVolatile) \
  template(Unsafe_GetDoubleVolatile) \
  template(Unsafe_SetDoubleVolatile) \
  template(Unsafe_SetOrderedInt) \
  template(Unsafe_SetOrderedObject) \
  template(Unsafe_SetOrderedLong) \
  template(Unsafe_GetNativeAddress) \
  template(Unsafe_SetNativeAddress) \
  template(Unsafe_AllocateInstance) \
  template(Unsafe_AllocateMemory) \
  template(Unsafe_ReallocateMemory) \
  template(Unsafe_FreeMemory) \
  template(Unsafe_SetMemory) \
  template(Unsafe_SetMemory2) \
  template(Unsafe_CopyMemory) \
  template(Unsafe_CopyMemory2) \
  template(Unsafe_AddressSize) \
  template(Unsafe_PageSize) \
  template(Unsafe_ObjectFieldOffset) \
  template(Unsafe_StaticFieldOffset) \
  template(Unsafe_StaticFieldBaseFromField) \
  template(Unsafe_FieldOffset) \
  template(Unsafe_StaticFieldBaseFromClass) \
  template(Unsafe_EnsureClassInitialized) \
  template(Unsafe_ArrayBaseOffset) \
  template(Unsafe_ArrayIndexScale) \
  template(Unsafe_DefineClass0) \
  template(Unsafe_DefineClass1) \
  template(Unsafe_TryMonitorEnter) \
  template(Unsafe_MonitorEnter) \
  template(Unsafe_MonitorExit) \
  template(Unsafe_ThrowException) \
  template(Unsafe_CompareAndSwapObject) \
  template(Unsafe_CompareAndSwapInt) \
  template(Unsafe_CompareAndSwapLong) \
  template(Unsafe_Park) \
  template(Unsafe_Unpark) \
  template(Unsafe_Loadavg) \
  template(Unsafe_PrefetchRead) \
  template(Unsafe_PrefetchWrite) \
  template(Unsafe_GetBoolean140) \
  template(Unsafe_SetBoolean140) \
  template(Unsafe_GetBoolean) \
  template(Unsafe_SetBoolean) \
  template(Unsafe_GetByte140) \
  template(Unsafe_SetByte140) \
  template(Unsafe_GetByte) \
  template(Unsafe_SetByte) \
  template(Unsafe_GetShort140) \
  template(Unsafe_SetShort140) \
  template(Unsafe_GetShort) \
  template(Unsafe_SetShort) \
  template(Unsafe_GetChar140) \
  template(Unsafe_SetChar140) \
  template(Unsafe_GetChar) \
  template(Unsafe_SetChar) \
  template(Unsafe_GetInt140) \
  template(Unsafe_SetInt140) \
  template(Unsafe_GetInt) \
  template(Unsafe_SetInt) \
  template(Unsafe_GetLong140) \
  template(Unsafe_SetLong140) \
  template(Unsafe_GetLong) \
  template(Unsafe_SetLong) \
  template(Unsafe_GetFloat140) \
  template(Unsafe_SetFloat140) \
  template(Unsafe_GetFloat) \
  template(Unsafe_SetFloat) \
  template(Unsafe_GetDouble140) \
  template(Unsafe_SetDouble140) \
  template(Unsafe_GetDouble) \
  template(Unsafe_SetDouble) \
  template(Unsafe_GetNativeByte) \
  template(Unsafe_SetNativeByte) \
  template(Unsafe_GetNativeShort) \
  template(Unsafe_SetNativeShort) \
  template(Unsafe_GetNativeChar) \
  template(Unsafe_SetNativeChar) \
  template(Unsafe_GetNativeInt) \
  template(Unsafe_SetNativeInt) \
  template(Unsafe_GetNativeLong) \
  template(Unsafe_SetNativeLong) \
  template(Unsafe_GetNativeFloat) \
  template(Unsafe_SetNativeFloat) \
  template(Unsafe_GetNativeDouble) \
  template(Unsafe_SetNativeDouble)

#define PERF_TAGS_DO(template) \
  template(Perf_Attach) \
  template(Perf_Detach) \
  template(Perf_CreateLong) \
  template(Perf_CreateByteArray) \
  template(Perf_HighResCounter) \
  template(Perf_HighResFrequency)

#define IRT_TAGS_DO(template) \
  template(remoteJNI__init_remotejni_method) \
  template(InterpreterRuntime___breakpoint) \
  template(InterpreterRuntime___new) \
  template(InterpreterRuntime__anewarray) \
  template(InterpreterRuntime__at_safepoint) \
  template(InterpreterRuntime__create_exception) \
  template(InterpreterRuntime__create_klass_exception) \
  template(InterpreterRuntime__create_native_wrapper) \
  template(InterpreterRuntime__frequency_counter_overflow) \
  template(InterpreterRuntime__get_original_bytecode_at) \
  template(InterpreterRuntime__interpreter_contains) \
  template(InterpreterRuntime__ldc) \
  template(InterpreterRuntime__ldc_w) \
  template(InterpreterRuntime__multianewarray) \
  template(InterpreterRuntime__post_field_access) \
  template(InterpreterRuntime__post_field_modification) \
  template(InterpreterRuntime__profile_method) \
  template(InterpreterRuntime__quicken_io_cc) \
  template(InterpreterRuntime__register_finalizer) \
  template(InterpreterRuntime__resolve_get_put) \
  template(InterpreterRuntime__resolve_invoke) \
  template(InterpreterRuntime__set_original_bytecode_at) \
  template(InterpreterRuntime__throw_AbstractMethodError) \
  template(InterpreterRuntime__throw_ArrayIndexOutOfBoundsException) \
  template(InterpreterRuntime__throw_ClassCastException) \
  template(InterpreterRuntime__throw_IllegalMonitorStateException) \
  template(InterpreterRuntime__throw_IncompatibleClassChangeError) \
  template(InterpreterRuntime__update_mdp_for_ret) \
  template(InterpreterRuntime__verify_mdp)

#define JRT_TAGS_DO(template) \
  template(InterpreterRuntime__fixup_callers_callsite) \
  template(JNIHandles__resolve_non_null_weak) \
  template(StubRoutines__find_NPE_continuation_address) \
  template(Runtime1__frequency_counter_overflow_wrapper_id) \
  template(Runtime1__new_instance) \
  template(Runtime1__new_instance_sba) \
  template(Runtime1__new_type_array) \
  template(Runtime1__new_type_array_sba) \
  template(Runtime1__new_object_array) \
  template(Runtime1__new_object_array_sba) \
  template(Runtime1__new_multi_array) \
  template(Runtime1__unimplemented_entry) \
  template(Runtime1__throw_array_store_exception) \
  template(Runtime1__post_jvmti_exception_throw) \
  template(Runtime1__exception_handler_for_pc) \
  template(Runtime1__throw_range_check_exception) \
  template(Runtime1__throw_index_exception) \
  template(Runtime1__throw_div0_exception) \
  template(Runtime1__implicit_throw_div0_exception) \
  template(Runtime1__implicit_throw_null_exception) \
  template(Runtime1__throw_stack_overflow) \
  template(Runtime1__throw_abstract_method_error) \
  template(Runtime1__throw_null_pointer_exception) \
  template(Runtime1__throw_class_cast_exception) \
  template(Runtime1__throw_incompatible_class_change_error) \
  template(Runtime1__monitorenter) \
  template(Runtime1__move_klass_patching) \
  template(Runtime1__patch_code) \
  template(Runtime1__access_field_patching) \
  template(Runtime1__init_check_patching) \
  template(Runtime1__frequency_counter_overflow) \
  template(OptoRuntime__multianewarray1_Java) \
  template(OptoRuntime__multianewarray2_Java) \
  template(OptoRuntime__multianewarray3_Java) \
  template(OptoRuntime__multianewarray4_Java) \
  template(OptoRuntime__multianewarray5_Java) \
  template(SharedRuntime___new) \
  template(SharedRuntime__throw_NPE) \
  template(SharedRuntime__build_StackOverflowError) \
  template(SharedRuntime__trace_bytecode) \
  template(SharedRuntime__yield_all) \
  template(SharedRuntime__inflate_monitor) \
  template(SharedRuntime__inflate_monitor_trace) \
  template(SharedRuntime__append_monitor_trace) \
  template(SharedRuntime__print_monitor_trace) \
  template(SharedRuntime__wait_for_monitor) \
  template(SharedRuntime__monitorenter) \
  template(SharedRuntime__monitorexit) \
  template(SharedRuntime__resolve_and_patch_call) \
  template(SharedRuntime__lazy_c2i) \
  template(SharedRuntime__record_memory_traces) \
  template(StubRoutines__jbyte_copy) \
  template(StubRoutines__jshort_copy) \
  template(StubRoutines__jint_copy) \
  template(StubRoutines__jlong_copy) \
  template(StubRoutines__objectRef_copy) \
  template(StubRoutines__arrayof_jbyte_copy) \
  template(StubRoutines__arrayof_jshort_copy) \
  template(StubRoutines__arrayof_jint_copy) \
  template(StubRoutines__arrayof_jlong_copy) \
  template(StubRoutines__arrayof_objectRef_copy) \
  template(Runtime1__compute_exception_pc) \
  template(Runtime1__monitorexit) \
  template(Runtime1__trace_method_entry) \
  template(Runtime1__trace_method_exit) \
  template(Runtime1__trace_block_entry) \
  template(Runtime1__arraycopy) \
  template(SharedRuntime__dsin) \
  template(SharedRuntime__dcos) \
  template(SharedRuntime__dlog) \
  template(SharedRuntime__dlog10) \
  template(SharedRuntime__dexp) \
  template(SharedRuntime__dpow) \
  template(SharedRuntime__dtan) \
  template(SharedRuntime__lmul) \
  template(SharedRuntime__ldiv) \
  template(SharedRuntime__lrem) \
  template(SharedRuntime__frem) \
  template(SharedRuntime__drem) \
  template(SharedRuntime__f2i) \
  template(SharedRuntime__f2l) \
  template(SharedRuntime__d2i) \
  template(SharedRuntime__d2l) \
  template(SharedRuntime__d2f) \
  template(SharedRuntime__l2f) \
  template(SharedRuntime__l2d) \
  template(SharedRuntime__handle_array_index_check) \
  template(SharedRuntime__collect_arraycopy_stats) \
  template(SharedRuntime__slow_arraycopy_C) \
  template(SharedRuntime__reguard_yellow_pages) \
  template(SharedRuntime__OSR_migration_begin) \
  template(SharedRuntime__OSR_migration_end) \
  template(SharedRuntime__register_finalizer) \
  template(PauselessNMT__nmt_trap) \
  template(InterpreterRuntime__find_exception_handler) \
  template(InterpreterRuntime__monitorexit) \
  template(InterpreterRuntime__new_IllegalMonitorStateException) \
  template(Deoptimization__uncommon_trap) \
  template(Deoptimization__deoptimize) \
  template(Deoptimization__popframe_preserve_args) \
  template(SharedRuntime__find_exception_handler_in_methodCode_pc) \
  template(SharedRuntime__sba_escape) \
  template(TickProfiler__tick) \
  template(PauselessNMT__nmt_trap_debug) \
  template(PauselessCollector__mark_new_ref) \
  template(GPGC_Collector__java_lang_ref_Referent_get_slow_path) \
  template(GPGC_NMT__new_space_nmt_buffer_full) \
  template(GPGC_NMT__old_space_nmt_buffer_full) \
  template(GPGC_NMT__sanity_check) \
  template(GPGC_TLB__lvb_trap_from_asm) \
  template(SharedRuntime__verify_oop) \
  template(SharedRuntime__verify_ref_klass) \
  template(SharedRuntime__post_method_entry) \
  template(SharedRuntime__post_method_exit)  \
  template(SharedRuntime__jvmti_contended_monitor_enter) \
  template(SharedRuntime__jvmti_contended_monitor_entered) \
  template(__patch_code) \
  template(SharedRuntime__live_object_profile_grow)

#define TAG_ENUM_DO(name)\
  name##_tag,

enum {
  no_tag = 0,
  unknownThread_tag,
  vmops_tags_start,
  VMOPS_TAGS_DO(TAG_ENUM_DO)
  vmops_tags_end,
  vm_tags_start,
  VM_TAGS_DO(TAG_ENUM_DO)
  vm_tags_end,
  jni_tags_start,
  JNI_TAGS_DO(TAG_ENUM_DO)
  jni_tags_end,
  jvm_tags_start,
  JVM_TAGS_DO(TAG_ENUM_DO)
  jvm_tags_end,
  unsafe_tags_start,
  UNSAFE_TAGS_DO(TAG_ENUM_DO)
  unsafe_tags_end,
  perf_tags_start,
  PERF_TAGS_DO(TAG_ENUM_DO)
  perf_tags_end,
  irt_tags_start,
  IRT_TAGS_DO(TAG_ENUM_DO)
  irt_tags_end,
  jrt_tags_start,
  JRT_TAGS_DO(TAG_ENUM_DO)
  jrt_tags_end,
  max_tag
};

class vmTags:AllStatic{
 private:
  static const char* _tag_names[max_tag+1];
 public:
  static bool is_native_call(int tag) {return (tag < no_tag) || (tag >= max_tag);}
  static const char* name_for(int tag);
};


#define VM_TICKS_DO(template) \
  template(thread_preemption_request) \
  template(thread_preempted) \
  template(thread_scheduled) \
  template(thread_exit) \
  template(vmlock_blocked) \
  template(vmlock_acquired) \
  template(vmlock_released) \
  template(vmlock_wait) \
  template(vmlock_wakeup) \
  template(vmlock_notify) \
  template(vmlock_notify_all) \
  template(vmlock_notify_nobody_home) \
  template(objectmonitor_revoke_bias_dead) \
  template(objectmonitor_revoke_bias_remote) \
  template(objectmonitor_revoke_bias_self) \
  template(objectmonitor_lock_wait) \
  template(objectmonitor_lock_acquired) \
  template(objectmonitor_unlock) \
  template(objectmonitor_wait_block) \
  template(objectmonitor_wait_wakeup) \
  template(objectmonitor_wait_notify) \
  template(objectmonitor_wait_broadcast) \
  template(os_sleep_begin) \
  template(os_sleep_end) \
  template(tlab_park) \
  template(tlab_unpark) \


#define TICK_ENUM_DO(name)\
  name##_tick,

enum {
  no_tick = 0,
  VM_TICKS_DO(TICK_ENUM_DO)
  max_tick
};

class vmTicks:AllStatic{
 private:
  static const char* _tick_names[max_tick+1];
 public:
  static const char* name_for(int tick);
};

#endif // VMTAGS_HPP
