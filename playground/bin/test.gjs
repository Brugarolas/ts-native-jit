class test<subtype> {
    constructor(subtype _x) {
        this.x = _x;
    }

    void print() {
        print_f32(666, this.x);
        testvec(this);
    }

    subtype x;
};

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

    test<f32> ayy = test<f32>(1.23f);
    ayy.print();

    f.x = 61;
    f.z = 2;
    f.y = 69;
    print_f32(61, f);

    testvec(vec3(2.0f));
    testvec(vec3(f));

    for(u8 i = 0;i < x.length;i++) {
        print_f32(i, x[i]);
    }
}
