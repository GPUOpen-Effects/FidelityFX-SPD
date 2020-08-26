project(${PROJECT_NAME})
include(${CMAKE_CURRENT_SOURCE_DIR}/../../common.cmake)

add_compile_options(/MP)

set(sources
    CSDownsampler.cpp
    CSDownsampler.h
    PSDownsampler.cpp
    PSDownsampler.h
    SPDCS.cpp
    SPDCS.h
    SPDSample.cpp
    SPDSample.h
    SPDRenderer.cpp
    SPDRenderer.h
    SPDVersions.cpp
    SPDVersions.h
    stdafx.cpp
    stdafx.h)
set(Shaders_src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../ffx-spd/ffx_a.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../ffx-spd/ffx_spd.h
    ${CMAKE_CURRENT_SOURCE_DIR}/CSDownsampler.glsl
    ${CMAKE_CURRENT_SOURCE_DIR}/PSDownsampler.glsl
    ${CMAKE_CURRENT_SOURCE_DIR}/SPDIntegration.glsl
    ${CMAKE_CURRENT_SOURCE_DIR}/SPDIntegration.hlsl
    ${CMAKE_CURRENT_SOURCE_DIR}/SPDIntegrationLinearSampler.glsl
    ${CMAKE_CURRENT_SOURCE_DIR}/SPDIntegrationLinearSampler.hlsl
)
set(Common_src
    ${CMAKE_CURRENT_SOURCE_DIR}/../Common/SpdSample.json
)

source_group("Sources"            FILES ${sources})    
source_group("Shaders"            FILES ${Shaders_src})
source_group("Common"             FILES ${Common_src})

# prevent VS from processing/compiling these files
set_source_files_properties(${Shaders_src} PROPERTIES VS_TOOL_OVERRIDE "Text")
set_source_files_properties(${Common_src} PROPERTIES VS_TOOL_OVERRIDE "Text")

function(copyCommand list dest)
	foreach(fullFileName ${list})    
    	get_filename_component(file ${fullFileName} NAME)
	    message("Generating custom command for ${fullFileName}")
	    add_custom_command(
		    OUTPUT   ${dest}/${file}
 			PRE_BUILD
 			COMMAND cmake -E make_directory ${dest}
 			COMMAND cmake -E copy ${fullFileName}  ${dest}
 			MAIN_DEPENDENCY  ${fullFileName}
 			COMMENT "Updating ${file} into ${dest}" 
 		)
	endforeach()    
endfunction()

# copy shaders and media to Bin
# include("${CMAKE_HOME_DIRECTORY}/src/Common/Shaders/CMakeList.txt")
copyCommand("${Shaders_src}" ${CMAKE_HOME_DIRECTORY}/bin/ShaderLibVK)
copyCommand("${Common_src}" ${CMAKE_HOME_DIRECTORY}/bin)

add_executable(${PROJECT_NAME} WIN32 ${sources} ${Shaders_src} ${Common_src}) 
target_link_libraries (${PROJECT_NAME} LINK_PUBLIC Cauldron_VK ImGUI Vulkan::Vulkan)
target_include_directories (${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../../../ffx-spd)
set_target_properties(${PROJECT_NAME} PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_HOME_DIRECTORY}/bin")
addManifest(${PROJECT_NAME})