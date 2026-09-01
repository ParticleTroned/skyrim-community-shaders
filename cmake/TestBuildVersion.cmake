function(csx_parse_test_build value out_number out_date)
    if("${value}" STREQUAL "")
        set("${out_number}" "" PARENT_SCOPE)
        set("${out_date}" "" PARENT_SCOPE)
        return()
    endif()

    if(
        NOT
            "${value}"
            MATCHES
            [[^(RC[1-9][0-9]*)-([0-9][0-9][0-9][0-9])-([0-9][0-9])-([0-9][0-9])$]]
    )
        message(
            FATAL_ERROR
            "CSX_TEST_BUILD must use the RC<number>-<YYYY-MM-DD> format."
        )
    endif()

    set(_number "${CMAKE_MATCH_1}")
    set(_year_text "${CMAKE_MATCH_2}")
    set(_month_text "${CMAKE_MATCH_3}")
    set(_day_text "${CMAKE_MATCH_4}")
    math(EXPR _year "1${_year_text} - 10000")
    math(EXPR _month "1${_month_text} - 100")
    math(EXPR _day "1${_day_text} - 100")
    if(_year LESS 1 OR _month LESS 1 OR _month GREATER 12 OR _day LESS 1)
        message(FATAL_ERROR "CSX_TEST_BUILD must contain a real calendar date.")
    endif()

    set(_month_days 31 28 31 30 31 30 31 31 30 31 30 31)
    math(EXPR _month_index "${_month} - 1")
    list(GET _month_days "${_month_index}" _maximum_day)
    math(EXPR _mod_four "${_year} % 4")
    math(EXPR _mod_hundred "${_year} % 100")
    math(EXPR _mod_four_hundred "${_year} % 400")
    if(
        _month EQUAL 2
        AND _mod_four EQUAL 0
        AND (NOT _mod_hundred EQUAL 0 OR _mod_four_hundred EQUAL 0)
    )
        set(_maximum_day 29)
    endif()
    if(_day GREATER _maximum_day)
        message(FATAL_ERROR "CSX_TEST_BUILD must contain a real calendar date.")
    endif()

    set("${out_number}" "${_number}" PARENT_SCOPE)
    set(
        "${out_date}"
        "${_year_text}-${_month_text}-${_day_text}"
        PARENT_SCOPE
    )
endfunction()
