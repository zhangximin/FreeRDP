# FreeRDP: A Remote Desktop Protocol Implementation
# FreeRDP cmake build script
#
# Copyright 2014  <zhangximin@gmail.com>
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

define_channel_client("jdisp")

set(${MODULE_PREFIX}_SRCS
	jdisp_pointer.c
	jdisp_pointer.h
	jdisp_main.c
	jdisp_main.h)

add_channel_client_library(${MODULE_PREFIX} ${MODULE_NAME} ${CHANNEL_NAME} TRUE "VirtualChannelEntry")

#set_target_properties(${MODULE_NAME} PROPERTIES PREFIX "")

#set_complex_link_libraries(VARIABLE ${MODULE_PREFIX}_LIBS
#	MONOLITHIC ${MONOLITHIC_BUILD}
#	MODULE freerdp
#	MODULES freerdp-utils)

#set(FREERDP_MXC_JPEG_LIBS mxc_jpeg lzo2)
#set(${MODULE_PREFIX}_LIBS
#	${FREERDP_MXC_JPEG_LIBS})
set(${MODULE_PREFIX}_LIBS ${${MODULE_PREFIX}_LIBS} mxc_jpeg lzo2)

target_link_libraries(${MODULE_NAME} ${${MODULE_PREFIX}_LIBS})

install(TARGETS ${MODULE_NAME} DESTINATION ${FREERDP_ADDIN_PATH} EXPORT FreeRDPTargets)

set_property(TARGET ${MODULE_NAME} PROPERTY FOLDER "Channels/${CHANNEL_NAME}/Client")
