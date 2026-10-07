"""R32Float linear 2D texel operations; ordinary NVIDIA global memory code.

The producer owns a 256-byte read-only descriptor buffer. Each texture has a
32-byte record: magic, version, width, height, pitch, pixelBytes, format, extent.
Data pointers start at the texture view offset. Coordinates and descriptor
fields are checked before any data access. This is not sampler support.
"""
MAGIC = 0x54583225
FORMAT = 3  # RTXTexture224::Format::R32Float, not an MTLPixelFormat enum value.


class TextureEmitter:
    def __init__(self, code, register, require, images, descriptors):
        self.code, self.reg, self.require = code, register, require
        self.images, self.descriptors = images, descriptors
        self.ordinal = 0

    def access(self, image, coord, *, result=None, source=None):
        self.require(image in self.images and len(coord) == 2, 'texture operand')
        self.require((result is None) != (source is None), 'texture operation')
        base, record, binding = self.images[image]
        r, c = self.reg, self.code
        fields = [r(('uint',32)) for _ in range(8)]
        addr = r(('uint',64))
        c.append(f'add.u64 {addr}, {self.descriptors}, {record*32};')
        for i, field in enumerate(fields): c.append(f'ld.global.u32 {field}, [{addr}+{i*4}];')
        magic, version, width, height, pitch, pixel, fmt, extent = fields
        end = 'TEX225_END_' + str(self.ordinal); self.ordinal += 1
        if result is not None:
            for lane in result: c.append(f'mov.f32 {lane}, 0f00000000;')

        def reject(comparison, x, y):
            p=r(('bool',));c.extend([f'setp.{comparison}.u32 {p}, {x}, {y};',f'@{p} bra {end};'])

        reject('ne',magic,MAGIC);reject('ne',version,1)
        reject('ne',pixel,4);reject('ne',fmt,FORMAT)
        reject('eq',width,0);reject('gt',width,16384)
        reject('eq',height,0);reject('gt',height,16384)
        reject('gt',extent,16777216)
        mask=r(('uint',32)); row=r(('uint',32))
        c.extend([f'and.b32 {mask}, {pitch}, 255;',f'mul.lo.u32 {row}, {width}, 4;'])
        reject('ne',mask,0);reject('lt',pitch,row)
        full=r(('uint',64));wide_extent=r(('uint',64));bad=r(('bool',))
        c.extend([f'mul.wide.u32 {full}, {pitch}, {height};',
                  f'cvt.u64.u32 {wide_extent}, {extent};',
                  f'setp.ne.u64 {bad}, {full}, {wide_extent};',f'@{bad} bra {end};'])
        # MSL 2026 section 6.13, pp.239-240: absent components keep their
        # default even for an out-of-bounds read; R32 alpha is always 1.0.
        if result is not None: c.append(f'mov.f32 {result[3]}, 0f3f800000;')
        reject('ge',coord[0],width);reject('ge',coord[1],height)
        rowoff=r(('uint',64));coloff=r(('uint',64));offset=r(('uint',64));location=r(('uint',64))
        c.extend([f'mul.wide.u32 {rowoff}, {coord[1]}, {pitch};',
                  f'mul.wide.u32 {coloff}, {coord[0]}, 4;',
                  f'add.u64 {offset}, {rowoff}, {coloff};',f'add.u64 {location}, {base}, {offset};'])
        if result is not None:
            c.append(f'ld.global.f32 {result[0]}, [{location}];')
        else: c.append(f'st.global.f32 [{location}], {source[0]};')
        c.append(end + ':')
        return binding
