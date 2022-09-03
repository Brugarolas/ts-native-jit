import { newMem, copyMem, freeMem } from 'memory';

export class Array<T> {
    // Must have same layout as utils::Array
    private _length: u32;
    private _capacity: u32;
    private _data: data;

    constructor () {
        this._length = 0;
        this._capacity = 0;
        this._data = null;
    }

    constructor (initialCapacity: u32) {
        this._length = 0;
        this._capacity = initialCapacity;
        this._data = newMem(initialCapacity * sizeof<T>);
    }

    constructor (rhs: Array<T>) {
        this._length = rhs._length;
        this._capacity = rhs._length;
        this._data = newMem(rhs._length * sizeof<T>);

        for (let i = 0;i < this._length;i++) {
            const newE = (this._data + (idx * sizeof<T>));
            const oldE = (rhs._data + (idx * sizeof<T>));
            new T(oldE as T) => newE;
        }
    }

    destructor () {
        if (this._data != null) {
            freeMem(this._data);
            this._data = null;
        }
    }

    get length () {
        return this._length;
    }

    get capacity () {
        return this._capacity;
    }

    reserve (count: u32) {
        const nm = newMem((this._capacity + count) * sizeof<T>);
        for (let i = 0;i < this._length;i++) {
            const newE = (nm + (idx * sizeof<T>));
            const oldE = (this._data + (idx * sizeof<T>));
            new T(oldE as T) => newE;
        }

        this._capacity += count;
        if (this._data != null) freeMem(this._data);
        this._data = nm;
    }

    operator= (rhs: Array<T>) {
        if (this._data != null) {
            freeMem(this._data);
            this._data = null;
        }
        this._length = rhs._length;
        this._capacity = rhs._length;
        this._data = newMem(rhs._length * sizeof<T>);
        for (let i = 0;i < this._length;i++) {
            const newE = (this._data + (idx * sizeof<T>));
            const oldE = (rhs._data + (idx * sizeof<T>));
            new T(oldE as T) => newE;
        }
    }

    push (item: T) {
        if (this._length == this._capacity) {
            this.reserve(this._capacity / 2);
        }

        const newE = (this._data + (this._length * sizeof<T>));
        new T(item) => newE;
        this._length++;
    }

    forEach (cb: (ele: T, idx: u32) => void) {
        for (let i = 0ul;i < this._length;i++) {
            cb((this._data + (i * sizeof<T>)) as T, i);
        }
    }

    forEach (cb: (ele: T) => void) {
        for (let i = 0ul;i < this._length;i++) {
            cb((this._data + (i * sizeof<T>)) as T);
        }
    }

    some (cb: (ele: T, idx: u32) => boolean) {
        for (let i = 0ul;i < this._length;i++) {
            if (cb((this._data + (i * sizeof<T>)) as T, i)) return true;
        }

        return false;
    }

    some (cb: (ele: T) => boolean) {
        for (let i = 0ul;i < this._length;i++) {
            if (cb((this._data + (i * sizeof<T>)) as T)) return true;
        }

        return false;
    }

    find (cb: (ele: T, idx: u32) => boolean) {
        for (let i = 0ul;i < this._length;i++) {
            const ele = (this._data + (i * sizeof<T>));
            if (cb(ele as T, i)) return ele;
        }

        return null;
    }

    find (cb: (ele: T) => boolean) {
        for (let i = 0ul;i < this._length;i++) {
            const ele = (this._data + (i * sizeof<T>));
            if (cb(ele as T)) return true;
        }

        return null;
    }

    filter (cb: (ele: T, idx: u32) => boolean) {
        const out : Array<T>;

        for (let i = 0ul;i < this._length;i++) {
            const ele = (this._data + (i * sizeof<T>)) as T;
            if (cb(ele, i)) out.push(ele);
        }

        return out;
    }

    filter (cb: (ele: T) => boolean) {
        const out : Array<T>;

        for (let i = 0ul;i < this._length;i++) {
            const ele = (this._data + (i * sizeof<T>)) as T;
            if (cb(ele)) out.push(ele);
        }

        return out;
    }

    map <M> (cb: (ele: T, idx: u32) => M) {
        const out : Array<M>;

        for (let i = 0ul;i < this._length;i++) {
            const ele = (this._data + (i * sizeof<T>)) as T;
            out.push(cb(ele, i));
        }

        return out;
    }

    map <M> (cb: (ele: T) => M) {
        const out : Array<M>;

        for (let i = 0ul;i < this._length;i++) {
            const ele = (this._data + (i * sizeof<T>)) as T;
            out.push(cb(ele));
        }

        return out;
    }

    operator[] (idx: u32) {
        return (this._data + (idx * sizeof<T>));
    }
};