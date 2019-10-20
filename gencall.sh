#!/bin/awk -f

BEGIN {
    print "#include <stdio.h>"
    print "#include <stdarg.h>"
    print "#include \"co.h\""
    print ""
}
#int printf(const char*, format, ...) vprintf
function trim(s)
{
    sub(/^[ \t\r\n]+/, "", s);
    sub(/[ \t\r\n]+$/, "", s);
    return s
}
{
    if(trim($0) == "") next
    if($0 ~ "#include") {
        print $0 >> "/dev/stderr"
        print $0
        next
    }
    rtype = $1
    fname = substr($2, 1, index($2, "(")-1)
    args_info = substr($0, index($0, "(")+1)
    args_info = substr(args_info, 1, index(args_info, ")")-1)
    n = split(args_info, args_array, ",")
    va_list = 0
    for(i=1; i<=n; i++)
        args_array[i] = trim(args_array[i])
    if(n && args_array[n] == "...") {
        n--
        va_list = 1
        va_func = $NF
    }
    for(i=1; i<=n; i++) {
        if(i%2 == 1) {
            typ = args_array[i]
            if(i == 1) {
                proto = typ
                struct = "        " typ
            } else {
                proto = proto ", " typ
                struct = struct "\n        " typ
            }
        }else{
            arg = args_array[i]
            proto = proto " " arg
            struct = struct " " arg ";"
            if(i == 2) {
                args = arg
                cargs = "args->" arg
                if(args_array[i-1] == "va_list") {
                    va_args = " "
                    va_copy = "    va_copy(__args_"NR"." arg ", " arg ");\n"
                } else {
                    va_args = arg
                    va_copy = ""
                }
            }else{
                args = args ", " arg
                cargs = cargs ", args->" arg
                if(args_array[i-1] == "va_list") {
                    va_args = va_args ", "
                    va_copy = va_copy "    va_copy(__args_"NR"." arg ", " arg ");\n"
                } else
                    va_args = va_args ", " arg
            }
        }
    }
    
    if(va_list == 1) {
        printf("%s co_%s(%s);\n", rtype, fname, n ? proto ", ...": "") >> "/dev/stderr"
        printf("%s co_%s(%s)\n", rtype, fname, n ? proto ", ...": "")
        printf("{\n")
        if(rtype != "void")
            printf("    %s __ret_"NR";\n", rtype)
        printf("    va_list __ap_"NR";\n")
        printf("    va_start(__ap_"NR", %s);\n", n ? args_array[n] : "")
        printf("    %sco_%s(%s, __ap_"NR");\n", rtype != "void" ? "__ret_"NR" = " : "", va_func, n ? args : "")
        printf("    va_end(__ap_"NR");\n")
        if(rtype != "void")
            printf("    return __ret_"NR";\n");
        printf("}\n")
        print ""
        next
    }
    
    printf("static void __co_%s(void *p)\n", fname)
    printf("{\n")
    if(rtype != "void" || n) {
        printf("    struct {\n")
        if(rtype != "void")
            printf("        %s ret;\n", rtype)
        if(n)
            printf("%s\n", struct)
        printf("    }*args = p;\n")
    }
    printf("    %s%s(%s);\n", rtype != "void" ? "args->ret = " : "", fname, n ? cargs : "")
    printf("}\n")
    
    printf("%s co_%s(%s);\n", rtype, fname, n ? proto : "") > "/dev/stderr"
    printf("%s co_%s(%s)\n", rtype, fname, n ? proto : "")
    printf("{\n")
    if(rtype != "void" || n) {
        printf("    struct {\n")
        if(rtype != "void")
            printf("        %s ret;\n", rtype)
        if(n)
            printf("%s\n", struct)
        printf("    } __args_"NR" = {%s%s%s};\n", rtype != "void" ? "0" : "",
            rtype != "void" && n ? ", " : "",
            n ? va_args : "")
        if(n) printf(va_copy)
        printf("    cocall(SHARESTACK, __co_%s, &__args_"NR");\n", fname)
    }else
        printf("    cocall(SHARESTACK, __co_%s, NULL);\n", fname)
    if(rtype != "void")
        printf("    return __args_"NR".ret;\n")
    printf("}\n")
    
    print ""
}