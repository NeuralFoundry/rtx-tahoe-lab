// Unsupported SDK operations explicitly record an error (queue methods raise).
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
NS_ASSUME_NONNULL_BEGIN
- (void)addResidencySet:(id <MTLResidencySet>)residencySet { [self unsupported:_cmd]; }
- (void)addResidencySets:(const id <MTLResidencySet> _Nonnull[_Nonnull])residencySets count:(NSUInteger)count { [self unsupported:_cmd]; }
- (void)removeResidencySet:(id <MTLResidencySet>)residencySet { [self unsupported:_cmd]; }
- (void)removeResidencySets:(const id <MTLResidencySet> _Nonnull[_Nonnull])residencySets count:(NSUInteger)count { [self unsupported:_cmd]; }
NS_ASSUME_NONNULL_END
#pragma clang diagnostic pop
