void it() {
    array<f32> x;
    x.push(1.23f);
    x.push(x[0]);
    x[0] = 4.56f;
    foo f;
    f.x = 5;
    f.x += 5;
    if (x.length > 0) {
        print_foo(f);
        x.push(5.55f);
    }

    for(u8 i = 0;i < 10;i++) {
        x.push(i);
    }

    for(u8 i = 0;i < x.length;i++) {
        print_f32(i, x[i]);
    }
}