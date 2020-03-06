project (SPDSample_VK)
include(${CMAKE_HOME_DIRECTORY}/common.cmake)
set(sources
    CSDownsampler.cpp
    CSDownsampler.h
    PSDownsampler.cpp
    PSDownsampler.h
    SPD_CS.cpp
    SPD_CS.h
    SPD_CS_Linear_Sampler.cpp
    SPD_CS_Linear_Sampler.h
    SPD_Sample.cpp
    SPD_Sample.h
    SPD_Renderer.cpp
    SPD_Renderer.h
    SPD_Versions.cpp
    SPD_Versions.h
    stdafx.cpp
    stdafx.h)
set(Shaders_src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../ffx-spd/ffx_a.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../ffx-spd/ffx_spd.h
    ${CMAKE_CURRENT_SOURCE_DIR}/CSDownsampler.glsl
    ${CMAKE_CURRENT_SOURCE_DIR}/PSDownsampler.glsl
    ${CMAKE_CURRENT_SOURCE_DIR}/SPD_Integration.glsl
    ${CMAKE_CURRENT_SOURCE_DIR}/SPD_Integration.hlsl
    ${CMAKE_CURRENT_SOURCE_DIR}/SPD_Integration_Linear_Sampler.glsl
    ${CMAKE_CURRENT_SOURCE_DIR}/SPD_Integration_Linear_Sampler.hlsl
)

source_group("Sources"            FILES ${sources})    
source_group("Shaders"            FILES ${Shaders_src})
# prevent VS from processing/compiling these files
set_source_files_properties(${Shaders_src} PROPERTIES VS_TOOL_OVERRIDE "Text")

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
    
add_executable(${PROJECT_NAME} WIN32 ${sources} ${Shaders_src}) 
target_link_libraries (${PROJECT_NAME} LINK_PUBLIC Cauldron_VK ImGUI Vulkan::Vulkan)
target_include_directories (${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../../../ffx-spd)
set_target_properties(${PROJECT_NAME} PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_HOME_DIRECTORY}/bin")
addManifest(${PROJECT_NAME})