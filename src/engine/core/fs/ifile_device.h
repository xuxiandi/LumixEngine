#pragma once

#include "engine/lumix.h"

namespace Lumix
{
namespace FS
{


class IFile;


class LUMIX_ENGINE_API IFileDevice
{
public:
	IFileDevice() {}
	virtual ~IFileDevice() {}

	virtual IFile* createFile(IFile* child) = 0;
	virtual void destroyFile(IFile* file) = 0;

	virtual const char* name() const = 0;
};


} // ~namespace FS
} // ~namespace Lumix
