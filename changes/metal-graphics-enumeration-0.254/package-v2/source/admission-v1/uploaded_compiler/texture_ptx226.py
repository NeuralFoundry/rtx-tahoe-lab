"""Runtime R32F/RGBA32F/RGBA8/BGRA8 texels on retained linear GPU backing.

Only formatless SPIR-V float images opt in. Explicit R32f retains its restriction.
UNORM conversion uses the MSL8.7 rules; no CPU conversion runs in the product.
"""
MAGIC=0x54583225
FLOAT_FORMATS=0x80000498  # Tagged mask of RTXTexture224 formats3,4,7,10.


class TextureEmitter:
    def __init__(self,code,register,require,images,descriptors):
        self.code,self.reg,self.require=code,register,require
        self.images,self.descriptors=images,descriptors
        self.ordinal=0

    def access(self,image,coord,*,result=None,source=None):
        self.require(image in self.images and len(coord)==2,'texture operand')
        self.require((result is None)!=(source is None),'texture operation')
        base,record,binding,allowed=self.images[image]
        self.require(allowed in (3,FLOAT_FORMATS),'texture format contract')
        c,r=self.code,self.reg;u32=lambda:r(('uint',32));u64=lambda:r(('uint',64));f32=lambda:r(('float',32));pred=lambda:r(('bool',))
        prefix='TEX226_'+str(self.ordinal);self.ordinal+=1;end=prefix+'_END'
        fields=[u32()for _ in range(8)];address=u64();c.append(f'add.u64 {address}, {self.descriptors}, {record*32};')
        for i,f in enumerate(fields):c.append(f'ld.global.u32 {f}, [{address}+{i*4}];')
        magic,version,width,height,pitch,pixel,fmt,extent=fields
        if result is not None:
            for lane in result:c.append(f'mov.f32 {lane}, 0f00000000;')
        def reject(op,x,y):
            p=pred();c.extend([f'setp.{op}.u32 {p}, {x}, {y};',f'@{p} bra {end};'])
        reject('ne',magic,MAGIC);reject('ne',version,1)
        is_r32,is_rgba,is_bgra,is_unorm=pred(),pred(),pred(),pred()
        c.extend([f'setp.eq.u32 {is_r32}, {fmt}, 3;',f'setp.eq.u32 {is_rgba}, {fmt}, 10;',f'setp.eq.u32 {is_bgra}, {fmt}, 7;',f'setp.eq.u32 {is_unorm}, {fmt}, 4;'])
        if allowed==3:reject('ne',fmt,3)
        else:
            valid,invalid=pred(),pred();c.extend([f'or.pred {valid}, {is_r32}, {is_rgba};',f'or.pred {valid}, {valid}, {is_bgra};',f'or.pred {valid}, {valid}, {is_unorm};',f'not.pred {invalid}, {valid};',f'@{invalid} bra {end};'])
        expected_pixel=u32();c.append(f'selp.u32 {expected_pixel}, 16, 4, {is_rgba};');reject('ne',pixel,expected_pixel)
        reject('eq',width,0);reject('gt',width,16384);reject('eq',height,0);reject('gt',height,16384);reject('gt',extent,16777216)
        mask,row=u32(),u32();c.extend([f'and.b32 {mask}, {pitch}, 255;',f'mul.lo.u32 {row}, {width}, {pixel};'])
        reject('ne',mask,0);reject('lt',pitch,row)
        full,wide_extent,bad=u64(),u64(),pred();c.extend([f'mul.wide.u32 {full}, {pitch}, {height};',f'cvt.u64.u32 {wide_extent}, {extent};',f'setp.ne.u64 {bad}, {full}, {wide_extent};',f'@{bad} bra {end};'])
        if result is not None:c.append(f'selp.f32 {result[3]}, 0f3f800000, 0f00000000, {is_r32};')
        reject('ge',coord[0],width);reject('ge',coord[1],height)
        rowoff,coloff,offset,location=u64(),u64(),u64(),u64();c.extend([f'mul.wide.u32 {rowoff}, {coord[1]}, {pitch};',f'mul.wide.u32 {coloff}, {coord[0]}, {pixel};',f'add.u64 {offset}, {rowoff}, {coloff};',f'add.u64 {location}, {base}, {offset};'])
        c.extend([f'@{is_r32} bra {prefix}_R32;',f'@{is_rgba} bra {prefix}_RGBA32;'])
        if result is not None:
            packed=u32();c.append(f'ld.global.u32 {packed}, [{location}];')
            for i,lane in enumerate(result):
                byte,converted,correction=u32(),f32(),f32()
                c.extend([f'bfe.u32 {byte}, {packed}, {i*8}, 8;',f'cvt.rn.f32.u32 {converted}, {byte};',
                          # hi+lo approximates1/255 beyond the precision needed to
                          # correctly round every one of the256 possible inputs.
                          f'mul.rn.f32 {correction}, {converted}, 0faf7efeff;',
                          f'fma.rn.f32 {lane}, {converted}, 0f3b808081, {correction};'])
            saved=f32();c.extend([f'mov.f32 {saved}, {result[0]};',f'selp.f32 {result[0]}, {result[2]}, {result[0]}, {is_bgra};',f'selp.f32 {result[2]}, {saved}, {result[2]}, {is_bgra};'])
        else:
            red,blue=f32(),f32();c.extend([f'selp.f32 {red}, {source[2]}, {source[0]}, {is_bgra};',f'selp.f32 {blue}, {source[0]}, {source[2]}, {is_bgra};'])
            packed=u32();c.append(f'mov.u32 {packed}, 0;')
            for i,lane in enumerate((red,source[1],blue,source[3])):
                wide,scaled,byte,shifted=u64(),u64(),u32(),u32()
                # f32*255 is exactly representable in f64. Integer conversion
                # rounds to u32. Saturating float conversion first clamps to[0,1], NaN->0.
                c.extend([f'cvt.sat.f64.f32 {wide}, {lane};',f'mul.rn.f64 {scaled}, {wide}, 0d406fe00000000000;',f'cvt.rni.u32.f64 {byte}, {scaled};',f'min.u32 {byte}, {byte}, 255;',f'shl.b32 {shifted}, {byte}, {i*8};',f'or.b32 {packed}, {packed}, {shifted};'])
            c.append(f'st.global.u32 [{location}], {packed};')
        c.extend([f'bra {end};',prefix+'_RGBA32:'])
        for i in range(4):
            c.append(f'ld.global.f32 {result[i]}, [{location}+{i*4}];'if result is not None else f'st.global.f32 [{location}+{i*4}], {source[i]};')
        c.extend([f'bra {end};',prefix+'_R32:'])
        c.append(f'ld.global.f32 {result[0]}, [{location}];'if result is not None else f'st.global.f32 [{location}], {source[0]};')
        c.append(end+':');return binding
