"""GPU LOD0 sampling; axes and taps share code to fit the native code allocation."""
MAGIC=0x534d3227
class SamplerEmitter:
 def __init__(self,code,register,require,textures,samplers):
  self.code,self.reg,self.require,self.textures,self.samplers=code,register,require,textures,samplers;self.ordinal=0
 def sample(self,image,sampler,coord,result):
  self.require(sampler in self.samplers and image in self.textures.images and len(coord)==2,'sample operands')
  c,r=self.code,self.reg;u32=lambda:r(('uint',32));u64=lambda:r(('uint',64));f32=lambda:r(('float',32));pred=lambda:r(('bool',))
  prefix='SAMPLE227_'+str(self.ordinal);self.ordinal+=1;end=prefix+'_END';tap_loop=prefix+'_TAP';axis_loop=prefix+'_AXIS'
  for v in result:c.append(f'mov.f32 {v}, 0f00000000;')
  address,binding=self.samplers[sampler];fields=[u32()for _ in range(8)]
  for i,v in enumerate(fields):c.append(f'ld.global.u32 {v}, [{address}+{4*i}];')
  magic,version,normalized,saddr,taddr,filtering,z0,z1=fields
  def reject(op,x,y):
   p=pred();c.extend([f'setp.{op}.u32 {p}, {x}, {y};',f'@{p} bra {end};'])
  reject('ne',magic,MAGIC);reject('ne',version,1);reject('gt',normalized,1);reject('gt',saddr,3);reject('gt',taddr,3);reject('gt',filtering,1);reject('ne',z0,0);reject('ne',z1,0)
  norm,linear=pred(),pred();c.extend([f'setp.eq.u32 {norm}, {normalized}, 1;',f'setp.eq.u32 {linear}, {filtering}, 1;'])
  _,record,texture_binding,_=self.textures.images[image]
  desc=u64();c.append(f'add.u64 {desc}, {self.textures.descriptors}, {record*32};');dims=[u32(),u32()]
  for i,v in enumerate(dims):
   c.append(f'ld.global.u32 {v}, [{desc}+{8+i*4}];');reject('eq',v,0);reject('gt',v,16384)
  tap,axis,x,y=u32(),u32(),u32(),u32();weight=f32()
  c.extend([f'mov.u32 {tap}, 0;',f'mov.u32 {x}, 0;',f'mov.u32 {y}, 0;',tap_loop+':','.pragma "nounroll";',f'mov.u32 {axis}, 0;',f'mov.f32 {weight}, 0f3f800000;',axis_loop+':','.pragma "nounroll";'])
  first=pred();source=f32();size,mode=u32(),u32()
  c.extend([f'setp.eq.u32 {first}, {axis}, 0;',f'selp.f32 {source}, {coord[0]}, {coord[1]}, {first};',f'selp.u32 {size}, {dims[0]}, {dims[1]}, {first};',f'selp.u32 {mode}, {saddr}, {taddr}, {first};'])
  raw,exponent=u32(),u32();c.extend([f'mov.b32 {raw}, {source};',f'and.b32 {exponent}, {raw}, 2139095040;']);reject('eq',exponent,2139095040)
  edge,repeat,mirror,wrap,unnorm,bad,clamp=pred(),pred(),pred(),pred(),pred(),pred(),pred()
  c.extend([f'setp.eq.u32 {edge}, {mode}, 0;',f'setp.eq.u32 {repeat}, {mode}, 1;',f'setp.eq.u32 {mirror}, {mode}, 2;',f'or.pred {wrap}, {repeat}, {mirror};',f'not.pred {unnorm}, {norm};',f'and.pred {bad}, {wrap}, {unnorm};',f'@{bad} bra {end};',f'or.pred {clamp}, {edge}, {mirror};'])
  extent,scale,position=f32(),f32(),f32();c.extend([f'cvt.rn.f32.u32 {extent}, {size};',f'@{wrap} bra {prefix}_WRAP;'])
  upper=f32();c.extend([f'selp.f32 {scale}, {extent}, 0f3f800000, {norm};',f'mul.rn.f32 {position}, {source}, {scale};',f'add.rn.f32 {upper}, {extent}, 0f3f800000;',f'max.f32 {position}, {position}, 0fbf800000;',f'min.f32 {position}, {position}, {upper};',f'bra {prefix}_POSITION;',prefix+'_WRAP:'])
  periodic,whole,fraction,other=f32(),f32(),f32(),f32()
  c.extend([f'selp.f32 {scale}, 0f3f000000, 0f3f800000, {mirror};',f'mul.rn.f32 {periodic}, {source}, {scale};',f'cvt.rmi.f32.f32 {whole}, {periodic};',f'sub.rn.f32 {fraction}, {periodic}, {whole};',f'min.f32 {other}, {fraction}, 0f3f7fffff;',f'selp.f32 {fraction}, {other}, {fraction}, {repeat};',f'add.rn.f32 {other}, {fraction}, {fraction};',f'selp.f32 {fraction}, {other}, {fraction}, {mirror};',f'sub.rn.f32 {other}, 0f40000000, {fraction};',f'min.f32 {other}, {fraction}, {other};',f'selp.f32 {fraction}, {other}, {fraction}, {mirror};',f'mul.rn.f32 {position}, {fraction}, {extent};',prefix+'_POSITION:'])
  bias,back,frac=f32(),f32(),f32();integer=u32()
  c.extend([f'selp.f32 {bias}, 0f3f000000, 0f00000000, {linear};',f'sub.rn.f32 {position}, {position}, {bias};',f'cvt.rmi.s32.f32 {integer}, {position};',f'cvt.rn.f32.s32 {back}, {integer};',f'sub.rn.f32 {frac}, {position}, {back};'])
  bit,index,limit,clamped,wrapped=u32(),u32(),u32(),u32(),u32();positive,negative,past=pred(),pred(),pred()
  c.extend([f'shr.u32 {bit}, {tap}, {axis};',f'and.b32 {bit}, {bit}, 1;',f'add.u32 {index}, {integer}, {bit};',f'sub.u32 {limit}, {size}, 1;',f'max.s32 {clamped}, {index}, 0;',f'min.s32 {clamped}, {clamped}, {limit};',f'setp.lt.s32 {negative}, {index}, 0;',f'add.u32 {wrapped}, {index}, {size};',f'selp.u32 {wrapped}, {wrapped}, {index}, {negative};',f'setp.ge.u32 {past}, {wrapped}, {size};',f'sub.u32 {limit}, {wrapped}, {size};',f'selp.u32 {wrapped}, {limit}, {wrapped}, {past};',f'selp.u32 {index}, {wrapped}, {index}, {repeat};',f'selp.u32 {index}, {clamped}, {index}, {clamp};',f'selp.u32 {x}, {index}, {x}, {first};',f'selp.u32 {y}, {y}, {index}, {first};'])
  complement,component=f32(),f32();again=pred()
  c.extend([f'setp.eq.u32 {positive}, {bit}, 1;',f'sub.rn.f32 {complement}, 0f3f800000, {frac};',f'selp.f32 {component}, {frac}, {complement}, {positive};',f'mul.rn.f32 {weight}, {weight}, {component};',f'add.u32 {axis}, {axis}, 1;',f'setp.lt.u32 {again}, {axis}, 2;',f'@{again} bra {axis_loop};'])
  texel=[f32()for _ in range(4)];self.textures.access(image,(x,y),result=texel)
  c.append(f'@{linear} bra {prefix}_ACCUMULATE;')
  for dst,src in zip(result,texel):c.append(f'mov.f32 {dst}, {src};')
  c.extend([f'bra {end};',prefix+'_ACCUMULATE:'])
  for dst,src in zip(result,texel):c.append(f'fma.rn.f32 {dst}, {src}, {weight}, {dst};')
  c.extend([f'add.u32 {tap}, {tap}, 1;',f'setp.lt.u32 {again}, {tap}, 4;',f'@{again} bra {tap_loop};',end+':'])
  return texture_binding,binding
