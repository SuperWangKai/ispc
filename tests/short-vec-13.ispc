export uniform int width() { return programCount; }

export void f_fu(uniform float RET[], uniform float aFOO[], uniform float b) {
    uniform int<4> x = {1,2,3,4}, y = {b-5,b-5,b-5,b-5};

    RET[programIndex] = 0;

    int index = aFOO[programIndex];
    index = min(index, 3);

    x <<= 1;
    if (programIndex < 4)
        RET[programIndex] = x[programIndex];

/*CO    return x << 1;*/
/*CO    return c[0] ? 1 : 0;*/
/*CO    x = b;*/
/*CO    y = b;*/
/*CO    return x+y;*/
}

export void result(uniform float RET[]) {
    RET[programIndex] = 0;
    RET[0] = 2;
    RET[1] = 4;
    RET[2] = 6;
    RET[3] = 8;
}
