// SPDX-License-Identifier: Apache-2.0
/*
 * c2plugin-shim: the app side of the Codec2 platform store plugin
 * (libc2plugin_store.so) that GloDroid's v4l2_codec2 decoder needs. The
 * component asks for its own allocators for the output pool: 21
 * (V4L2_BUFFERQUEUE, to a surface) or 22 (V4L2_BUFFERPOOL, byte buffers).
 * The app's CCodec only checks that its allocator store knows the ID; the
 * pool itself is made by the codec service, which has the real plugin. The
 * system image has no plugin, so CCodec fell back to plain gralloc, which
 * this decoder can't use. This allocator just answers its ID.
 *
 * Only 22: with 21 the service's bufferqueue pool (C2VdaBqBlockPool) gets
 * back only 2 of its buffers after a flush, and a seek stalls the decoder.
 * Told 21 is unknown, CCodec keeps 22 for a surface too and attaches the
 * decoded buffers to the surface to show them.
 *
 * Built with the NDK, libc++ linked in statically. CCodec sees the allocator
 * only through its vtable, so C2Allocator is declared here with AOSP's
 * virtual methods in AOSP's order (media/codec2/core/include/C2Buffer.h);
 * types it doesn't use are opaque, and the methods that return library
 * types give empty values, which need no allocation.
 */
#include <cerrno>
#include <cstdint>
#include <memory>
#include <string>

typedef int32_t c2_status_t;
enum : c2_status_t {
	C2_OK = 0,
	C2_NOT_FOUND = ENOENT,
	C2_OMITTED = ENOSYS,
};
typedef void C2Handle;
struct C2MemoryUsage {
	uint64_t expected;
	uint64_t consumer;
};
class C2LinearAllocation;
class C2GraphicAllocation;

class C2Allocator {
public:
	typedef uint32_t id_t;
	struct Traits;

	virtual std::string getName() const = 0;
	virtual id_t getId() const = 0;
	virtual std::shared_ptr<const Traits> getTraits() const = 0;
	virtual c2_status_t newLinearAllocation(uint32_t, C2MemoryUsage,
						std::shared_ptr<C2LinearAllocation> *)
	{
		return C2_OMITTED;
	}
	virtual c2_status_t priorLinearAllocation(const C2Handle *,
						  std::shared_ptr<C2LinearAllocation> *)
	{
		return C2_OMITTED;
	}
	virtual c2_status_t newGraphicAllocation(uint32_t, uint32_t, uint32_t, C2MemoryUsage,
						 std::shared_ptr<C2GraphicAllocation> *)
	{
		return C2_OMITTED;
	}
	virtual c2_status_t priorGraphicAllocation(const C2Handle *,
						   std::shared_ptr<C2GraphicAllocation> *)
	{
		return C2_OMITTED;
	}
	virtual ~C2Allocator() = default;
	virtual bool checkHandle(const C2Handle *const handle) const = 0;

protected:
	C2Allocator() = default;
};

namespace {

constexpr C2Allocator::id_t kV4l2BufferPool = 22;

class IdOnlyAllocator : public C2Allocator {
public:
	explicit IdOnlyAllocator(id_t id) : mId(id) {}
	std::string getName() const override { return std::string(); }
	id_t getId() const override { return mId; }
	std::shared_ptr<const Traits> getTraits() const override { return nullptr; }
	bool checkHandle(const C2Handle *const) const override { return false; }

private:
	const id_t mId;
};

} // namespace

extern "C" __attribute__((visibility("default")))
C2Allocator *CreateAllocator(C2Allocator::id_t id, c2_status_t *status)
{
	if (id != kV4l2BufferPool) {
		*status = C2_NOT_FOUND;
		return nullptr;
	}
	*status = C2_OK;
	return new IdOnlyAllocator(id);
}
