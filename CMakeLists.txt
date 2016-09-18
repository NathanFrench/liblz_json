cmake_minimum_required(VERSION 2.8)

include_directories(${PROJECT_BINARY_DIR}/include)

find_library (LIBLUA
		      NAMES
				lua-5.1 
				lua5.1 
				luajit-5.1 
				luajit5.1)

find_path (LIBLUA_PATH
		   lua.h 
			/usr/include/lua5.1 
			/usr/local/include/lua5.1 
			/usr/include/luajit-2.0 
			/usr/local/include/luajit-2.0)

if (LIBLUA)
	if (LIBLUA_PATH)
		include_directories(${LIBLUA_PATH})
	endif()
endif()

add_subdirectory   (lib)
add_subdirectory   (src)
