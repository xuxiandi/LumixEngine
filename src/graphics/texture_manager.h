#pragma once

#include "core/resource_manager_base.h"

namespace Lux
{
	class LUX_ENGINE_API TextureManager : public ResourceManagerBase
	{
	public:
		TextureManager() : ResourceManagerBase() {}
		~TextureManager() {}

	protected:
		virtual Resource* createResource(const Path& path) LUX_OVERRIDE;
		virtual void destroyResource(Resource& resource) LUX_OVERRIDE;
	};
}