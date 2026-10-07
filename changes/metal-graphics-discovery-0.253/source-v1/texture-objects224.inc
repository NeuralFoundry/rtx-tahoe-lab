// Public Metal resource calls against the actual candidate bundle. The caller
// supplies the existing CPU broker model; no native GPU open occurs here.
static bool textureThrows224(const std::function<void()>&fn){@try{fn();return false;}@catch(NSException*e){return[e.name isEqualToString:NSInvalidArgumentException];}}
static MTLTextureDescriptor*descriptor224(MTLPixelFormat format,NSUInteger width,NSUInteger height){
 auto*d=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format width:width height:height mipmapped:NO];
 d.storageMode=MTLStorageModeShared;d.hazardTrackingMode=MTLHazardTrackingModeTracked;d.usage=MTLTextureUsageShaderRead|MTLTextureUsageShaderWrite;return d;
}
[[maybe_unused]] static void textures224(id<MTLDevice>device){
 const MTLPixelFormat formats[]={MTLPixelFormatR32Uint,MTLPixelFormatR32Sint,MTLPixelFormatR32Float,MTLPixelFormatRGBA8Unorm,MTLPixelFormatRGBA8Uint,MTLPixelFormatRGBA8Sint,MTLPixelFormatBGRA8Unorm,MTLPixelFormatRGBA32Uint,MTLPixelFormatRGBA32Sint,MTLPixelFormatRGBA32Float};
 for(unsigned formatIndex=0;formatIndex<10;++formatIndex){
  const NSUInteger pixel=formatIndex<7?4:16,stride=10*pixel;
  auto*d=descriptor224(formats[formatIndex],7,5);id<MTLTexture>texture=[device newTextureWithDescriptor:d];
  CHECK(texture&&[texture conformsToProtocol:@protocol(MTLTexture)]&&texture.device==device);
  CHECK(!texture.buffer&&!texture.rootResource&&!texture.parentTexture&&texture.bufferOffset==0&&texture.bufferBytesPerRow==0);
  CHECK(texture.width==7&&texture.height==5&&texture.depth==1&&texture.pixelFormat==formats[formatIndex]);
  CHECK(texture.mipmapLevelCount==1&&texture.sampleCount==1&&texture.arrayLength==1&&texture.storageMode==MTLStorageModeShared&&texture.allocatedSize>=1280);
  d.width=777;d.pixelFormat=MTLPixelFormatInvalid;CHECK(texture.width==7&&texture.pixelFormat==formats[formatIndex]);
  std::vector<std::uint8_t>source(stride*3+17),received(stride*3+17,0xa7),expected=received;
  for(std::size_t i=0;i<source.size();++i)source[i]=std::uint8_t(i*31+formatIndex);
  const auto region=MTLRegionMake2D(1,2,3,2);
  [texture replaceRegion:region mipmapLevel:0 slice:0 withBytes:source.data()bytesPerRow:stride bytesPerImage:0];
  [texture getBytes:received.data()bytesPerRow:stride bytesPerImage:0 fromRegion:region mipmapLevel:0 slice:0];
  for(unsigned row=0;row<2;++row)for(NSUInteger b=0;b<3*pixel;++b)expected[row*stride+b]=source[row*stride+b];CHECK(received==expected);
  CHECK(textureThrows224([&]{[texture replaceRegion:MTLRegionMake2D(6,4,2,1)mipmapLevel:0 withBytes:source.data()bytesPerRow:stride];}));
  CHECK(textureThrows224([&]{[texture getBytes:received.data()bytesPerRow:stride fromRegion:region mipmapLevel:1];}));
  CHECK(textureThrows224([&]{[texture replaceRegion:region mipmapLevel:0 withBytes:source.data()bytesPerRow:pixel];}));
  [texture replaceRegion:MTLRegionMake2D(7,5,0,0)mipmapLevel:0 withBytes:source.data()bytesPerRow:0];
  [texture setLabel:@"texture224"];CHECK([texture.label isEqualToString:@"texture224"]);
  CHECK([texture setPurgeableState:MTLPurgeableStateEmpty]==MTLPurgeableStateNonVolatile);
  CHECK(textureThrows224([&]{[texture getBytes:received.data()bytesPerRow:stride fromRegion:region mipmapLevel:0];}));
  CHECK([texture setPurgeableState:MTLPurgeableStateNonVolatile]==MTLPurgeableStateEmpty);
  std::vector<std::uint8_t>zero(7*5*pixel,0xcc);[texture getBytes:zero.data()bytesPerRow:7*pixel fromRegion:MTLRegionMake2D(0,0,7,5)mipmapLevel:0];CHECK(zero==std::vector<std::uint8_t>(zero.size(),0));
  [texture release];
 }
 CHECK([device minimumLinearTextureAlignmentForPixelFormat:MTLPixelFormatRGBA8Unorm]==256);
 CHECK([device minimumLinearTextureAlignmentForPixelFormat:MTLPixelFormatDepth32Float]==NSUIntegerMax);
 for(unsigned fault=0;fault<9;++fault){auto*d=descriptor224(MTLPixelFormatRGBA8Unorm,7,5);
  if(fault==0)d.storageMode=MTLStorageModePrivate;if(fault==1)d.textureType=MTLTextureType3D;
  if(fault==2)d.arrayLength=2;if(fault==3)d.mipmapLevelCount=2;if(fault==4)d.sampleCount=4;
  if(fault==5)d.usage=MTLTextureUsagePixelFormatView;if(fault==6)d.pixelFormat=MTLPixelFormatDepth32Float;
  if(fault==7)d.width=0;if(fault==8)d.hazardTrackingMode=MTLHazardTrackingModeUntracked;
  CHECK(![device newTextureWithDescriptor:d]);
 }
 auto*d=descriptor224(MTLPixelFormatRGBA8Unorm,7,5);id<MTLBuffer>buffer=[device newBufferWithLength:2048 options:MTLResourceStorageModeShared];CHECK(buffer);fill(buffer,17);
 CHECK(![buffer newTextureWithDescriptor:d offset:1 bytesPerRow:256]);CHECK(![buffer newTextureWithDescriptor:d offset:256 bytesPerRow:28]);CHECK(![buffer newTextureWithDescriptor:d offset:1024 bytesPerRow:256]);
 id<MTLTexture>view=[buffer newTextureWithDescriptor:d offset:256 bytesPerRow:256];CHECK(view&&view.buffer==buffer&&view.rootResource==buffer&&view.bufferOffset==256&&view.bufferBytesPerRow==256);
 std::vector<std::uint8_t>bytes(7*5*4);[view getBytes:bytes.data()bytesPerRow:28 fromRegion:MTLRegionMake2D(0,0,7,5)mipmapLevel:0];auto before=snapshot(buffer);
 for(NSUInteger row=0;row<5;++row)CHECK(!std::memcmp(bytes.data()+row*28,before.data()+256+row*256,28));
 auto expected=before;const auto*source=static_cast<const std::uint8_t*>(buffer.contents)+256;
 for(NSUInteger row=0;row<3;++row)for(NSUInteger column=0;column<12;++column)expected[256+row*256+4+column]=before[256+row*12+column];
 [view replaceRegion:MTLRegionMake2D(1,0,3,3)mipmapLevel:0 withBytes:source bytesPerRow:12];CHECK(snapshot(buffer)==expected);
 [buffer release];CHECK(view.buffer&&view.buffer.length==2048);[view release];
 // Texture views retain heap buffers, and aliasability invalidates every view.
 MTLHeapDescriptor*hd=[MTLHeapDescriptor new];hd.size=8192;hd.storageMode=MTLStorageModeShared;hd.hazardTrackingMode=MTLHazardTrackingModeTracked;
 id<MTLHeap>heap=[device newHeapWithDescriptor:hd];[hd release];CHECK(heap);buffer=[heap newBufferWithLength:2048 options:MTLResourceStorageModeShared];CHECK(buffer);
 view=[buffer newTextureWithDescriptor:d offset:256 bytesPerRow:256];CHECK(view&&view.heap==heap);[buffer makeAliasable];CHECK(view.isAliasable);
 CHECK(textureThrows224([&]{[view getBytes:bytes.data()bytesPerRow:28 fromRegion:MTLRegionMake2D(0,0,7,5)mipmapLevel:0];}));
 [view release];[buffer release];[heap release];
}
