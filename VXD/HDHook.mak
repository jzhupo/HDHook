# HDHook.mak - makefile for VxD HDHook

DEVICENAME = HDHook
DYNAMIC = 1
FRAMEWORK = C
DEBUG = 0
OBJECTS = HDHook.obj

!include $(VTOOLSD)\include\vtoolsd.mak
!include $(VTOOLSD)\include\vxdtarg.mak

HDHook.obj:	HDHook.c HDHook.h