#include "AHD.h"
#include "AHDUtils.h"
#include <vector>
#include <algorithm>
#include "AHDd3d11Helper.h"
#include <d3dcompiler.h>

#undef max
#undef min

using namespace AHD;

#pragma comment (lib,"d3d11.lib")
#pragma comment (lib,"d3dx11.lib")

#define EXCEPT(x) {throw std::exception(x);}

#define CHECK_RESULT(x, y) { if (FAILED(x)) EXCEPT(y); }
#define SAFE_RELEASE(x) {if(x) (x)->Release(); (x) = 0;}


typedef D3D11Helper Helper;


void DefaultEffect::init(ID3D11Device* device)
{
	{
		D3D11_INPUT_ELEMENT_DESC desc[] = { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 };

		ID3DBlob* blob;
		CHECK_RESULT(Helper::compileShader(&blob, "DefaultEffect.hlsl", "vs", "vs_5_0", NULL), 
					 "fail to compile vertex shader, cant use gpu voxelizer");
		CHECK_RESULT(device->CreateInputLayout(desc, ARRAYSIZE(desc), blob->GetBufferPointer(), blob->GetBufferSize(), &mLayout), 
					 "fail to create mLayout,  cant use gpu voxelizer");
		CHECK_RESULT(device->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(), NULL, &mVertexShader), 
					 "fail to create vertex shader,  cant use gpu voxelizer");
		blob->Release();
	}


	{
		ID3DBlob* blob;
		CHECK_RESULT(Helper::compileShader(&blob, "DefaultEffect.hlsl", "ps", "ps_5_0", NULL), 
					 "fail to compile pixel shader,  cant use gpu voxelizer");
		CHECK_RESULT(device->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), NULL, &mPixelShader), 
					 "fail to create pixel shader, cant use gpu voxelizer");

		blob->Release();
	}


	CHECK_RESULT(Helper::createBuffer(&mConstant, device, D3D11_BIND_CONSTANT_BUFFER, sizeof(XMMATRIX) * 3 + sizeof(float) * 3 + sizeof(size_t)), 
				 "fail to create constant buffer,  cant use gpu voxelizer");

}

void DefaultEffect::prepare(ID3D11DeviceContext* context)
{
	context->VSSetShader(mVertexShader, NULL, 0);
	context->IASetInputLayout(mLayout);
	context->PSSetShader(mPixelShader, NULL, 0);
	context->VSSetConstantBuffers(0, 1, &mConstant);
	context->PSSetConstantBuffers(0, 1, &mConstant);

}

void DefaultEffect::update(EffectParameter& paras)
{
	paras.context->UpdateSubresource(mConstant, 0, NULL, &paras.world, 0, 0);

}


void DefaultEffect::clean()
{
	mConstant->Release();
	mVertexShader->Release();
	mLayout->Release();
	mPixelShader->Release();
}

void VoxelResource::setVertex(const void* vertices, size_t vertexCount, size_t vertexStride, size_t posoffset )
{
	mVertexStride = vertexStride;
	mVertexCount = vertexCount;
	mPositionOffset = posoffset;

	//calculate the max size
	mAABB.setNull();
	size_t buffersize = vertexCount * vertexStride;
	{
		const char* begin = (const char*)vertices;
		const char* end = begin + buffersize;
		for (; begin != end; begin += vertexStride)
		{
			Vector3 v = (*(const Vector3*)begin) ;
			mAABB.merge(v);
		}
	}

	mNeedCalSize = false;

	mVertexBuffer.release();
	CHECK_RESULT(Helper::createBuffer(&mVertexBuffer, mDevice, D3D11_BIND_VERTEX_BUFFER, vertexCount * vertexStride, vertices),
				 "fail to create vertex buffer,  cant use gpu voxelizer");


}

void VoxelResource::setVertexFromVoxelResource(VoxelResource* res)
{
	mAABB = res->mAABB;

	setVertex(res->mVertexBuffer, res->mVertexCount, res->mVertexStride, res->mPositionOffset);
}

void VoxelResource::setVertex(ID3D11Buffer* vertexBuffer, size_t vertexCount, size_t vertexStride, size_t posoffset )
{
	mVertexStride = vertexStride;
	mVertexCount = vertexCount;
	mPositionOffset = posoffset;

	vertexBuffer->AddRef();
	mVertexBuffer.release();
	mVertexBuffer = vertexBuffer;

	mNeedCalSize = true;
}

void VoxelResource::setIndex(const void* indexes, size_t indexCount, size_t indexStride)
{
	mIndexCount = indexCount;
	mIndexStride = indexStride;

	mIndexBuffer.release();
	if (indexes != 0 && indexCount != 0 && indexStride != 0)
		CHECK_RESULT(Helper::createBuffer(&mIndexBuffer, mDevice, D3D11_BIND_INDEX_BUFFER, indexCount * indexStride, indexes),
				 "fail to create index buffer,  cant use gpu voxelizer");
}

void VoxelResource::setIndex(ID3D11Buffer* indexBuffer, size_t indexCount, size_t indexStride)
{
	mIndexCount = indexCount;
	mIndexStride = indexStride;

	indexBuffer->AddRef();
	mIndexBuffer.release();
	mIndexBuffer = indexBuffer;
}

VoxelResource::VoxelResource(ID3D11Device* device)
	:mDevice(device)
{

}

VoxelResource::~VoxelResource()
{

}

void VoxelResource::setEffect(Effect* effect)
{
	mEffect = effect;
}



void VoxelResource::prepare(ID3D11DeviceContext* context)
{
	if (!mNeedCalSize)
		return;

	if (mVertexBuffer == nullptr)
		return;


	mAABB.setNull();
	D3D11_BUFFER_DESC desc;
	mVertexBuffer->GetDesc(&desc);

	auto calSize = [this](ID3D11DeviceContext* context, ID3D11Buffer* buffer, size_t count , size_t stride, size_t offset)
	{
		D3D11_MAPPED_SUBRESOURCE mr;
		context->Map(buffer, 0, D3D11_MAP_READ, 0, &mr);

		size_t buffersize = count * stride;
		{
			const char* begin = (const char*)mr.pData;
			const char* end = begin + buffersize;
			for (; begin != end; begin += stride)
			{
				Vector3 v = (*(const Vector3*)(begin + offset));
				mAABB.merge(v);
			}
		}

		context->Unmap(buffer, 0);
	};

	if ((desc.CPUAccessFlags & D3D11_CPU_ACCESS_READ) != 0)
	{
		calSize(context, mVertexBuffer, mVertexCount, mVertexStride, mPositionOffset);
	}
	else
	{
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		desc.BindFlags = 0;
		desc.Usage = D3D11_USAGE_STAGING;
		ID3D11Buffer* tmp = 0;
		CHECK_RESULT(mDevice->CreateBuffer(&desc, nullptr, &tmp),
					 "fail to create temp buffer for reading.");
		context->CopyResource(tmp, mVertexBuffer);
		calSize(context, tmp, mVertexCount, mVertexStride, mPositionOffset);
		tmp->Release();
	}
}

VoxelOutput::VoxelOutput(ID3D11Device* device, ID3D11DeviceContext* context)
:mDevice(device), mContext(context)
{}

void VoxelOutput::addUAVTexture3D(size_t slot, DXGI_FORMAT format, size_t elementSize)
{
	UAV uav = { slot, format, elementSize ,true, ~0};
	if (!mUAVs.insert(std::make_pair(slot, uav)).second)
	{
		EXCEPT("the slot is using for other uav");
	}
}

void VoxelOutput::addUAVBuffer(size_t slot, size_t elementSize, size_t elementCount)
{
	UAV uav = { slot, DXGI_FORMAT_UNKNOWN, elementSize, false, elementCount };
	if (!mUAVs.insert(std::make_pair(slot, uav)).second)
	{
		EXCEPT("the slot is using for other uav");
	}
}


void VoxelOutput::removeUAV(size_t slot)
{
	mUAVs.erase(slot);
}

void VoxelOutput::exportData(VoxelData& data, size_t slot)
{
	auto ret = mUAVs.find(slot);
	if (ret == mUAVs.end())
		return;

	data.width = mWidth;
	data.height = mHeight;
	data.depth = mDepth;

	Interface<ID3D11Texture3D> debug = NULL;
	D3D11_TEXTURE3D_DESC dsDesc;
	ret->second.texture->GetDesc(&dsDesc);
	dsDesc.BindFlags = 0;
	dsDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	dsDesc.MiscFlags = 0;
	dsDesc.Usage = D3D11_USAGE_STAGING;

	CHECK_RESULT(mDevice->CreateTexture3D(&dsDesc, NULL, &debug),
				 "fail to create staging buffer, cant use gpu voxelizer");

	mContext->CopyResource(debug, ret->second.texture);
	D3D11_MAPPED_SUBRESOURCE mr;
	mContext->Map(debug, 0, D3D11_MAP_READ, 0, &mr);

	int stride = ret->second.para.elementSize * mWidth;
	data.datas.reserve(stride * mHeight * mDepth);
	char* begin = data.datas.data();
	for (size_t z = 0; z < mDepth; ++z)
	{
		const char* depth = ((const char*)mr.pData + mr.DepthPitch * z);
		for (size_t y = 0; y < mHeight; ++y)
		{
			memcpy(begin, depth + mr.RowPitch * y, stride);
			begin += stride;
		}
	}

	mContext->Unmap(debug, 0);

}

void VoxelOutput::prepare( int width, int height, int depth)
{
	mWidth = width;
	mHeight = height;
	mDepth = depth;

	for (auto& i : mUAVs)
	{
		UAV& uav = i.second;

		uav.texture.release();
		uav.uav.release();
		uav.buffer.release();
		if (uav.para.isTexture)
		{
			CHECK_RESULT(Helper::createUAVTexture3D(&uav.texture, &uav.uav, mDevice, uav.para.format, mWidth, mHeight, mDepth),
						 "failed to create uav texture3D,  cant use gpu voxelizer");
		}
		else
		{
			size_t count = std::min(uav.para.elementCount,(size_t) mWidth * mHeight * mDepth);
			CHECK_RESULT(Helper::createUAVBuffer( &uav.buffer, &uav.uav,mDevice, uav.para.elementSize, count),
						 "failed to create uav buffer, cannot use gpu voxelizer");
		}

		mContext->OMSetRenderTargetsAndUnorderedAccessViews(
			0, 0, NULL, uav.para.slot, 1, &uav.uav, NULL);
		UINT initcolor[4] = { 0 };
		mContext->ClearUnorderedAccessViewUint(uav.uav, initcolor);
	}
}

Voxelizer::Voxelizer()
{
	Helper::createDevice(&mDevice, &mContext);

}

Voxelizer::Voxelizer(ID3D11Device* device, ID3D11DeviceContext* context)
{
	mDevice = device;
	device->AddRef();
	mContext = context;
	context->AddRef();

}

Voxelizer::~Voxelizer()
{
	cleanResource();

	for (auto& i : mResources)
	{
		delete i;
	}

	for (auto& i : mOutputs)
	{
		delete i;
	}

	for (auto i : mEffects)
	{
		i->clean();
	}

}

void Voxelizer::cleanResource()
{


}


void Voxelizer::setVoxelSize(float v)
{
	mVoxelSize = v;
}

void Voxelizer::setScale(float v)
{
	mScale = v;
}

Vector3 Voxelizer::prepare(VoxelOutput* output, size_t count, VoxelResource** res)
{
	if (res == nullptr)
		return Vector3::ZERO;

	AABB aabb;
	for (size_t i = 0; i < count; ++i)
	{
		res[i]->prepare(mContext);
		aabb.merge(res[i]->mAABB);
	}


	float scale = mScale / mVoxelSize;
	Vector3 osize = aabb.getSize();
	//osize += Vector3::UNIT_SCALE;


	//transfrom
	Vector3 center = aabb.getCenter();


	mTranslation = XMMatrixTranspose(XMMatrixTranslation(-center.x, -center.y, -center.z));
	
	float ex = 2 / scale;
	osize.x += ex;
	osize.y += ex;
	osize.z += ex;



	output->prepare(osize.x * scale, osize.y* scale, osize.z * scale);

	return osize ;
}


void Voxelizer::voxelize(VoxelOutput* output, size_t count, VoxelResource** res)
{
	Vector3 range;
	if ((range = prepare(output, count, res)) == Vector3::ZERO)
	{
		EXCEPT(" cant use gpu voxelizer");
	}

	//no need to cull
	Interface<ID3D11RasterizerState> rasterizerState;
	{
		D3D11_RASTERIZER_DESC desc;
		desc.FillMode = D3D11_FILL_SOLID;
		desc.CullMode = D3D11_CULL_NONE;
		desc.FrontCounterClockwise = false;
		desc.DepthBias = 0;
		desc.DepthBiasClamp = 0;
		desc.SlopeScaledDepthBias = 0;
		desc.DepthClipEnable = true;
		desc.ScissorEnable = false;
		desc.MultisampleEnable = false;
		desc.AntialiasedLineEnable = false;

		CHECK_RESULT(mDevice->CreateRasterizerState(&desc, &rasterizerState),
					 "fail to create rasterizer state,  cant use gpu voxelizer");
		mContext->RSSetState(rasterizerState);
	}

	for (size_t i = 0; i < count; ++i)
	{
		voxelizeImpl(res[i], range);
	}

}

void Voxelizer::voxelizeImpl(VoxelResource* res, const Vector3& range)
{


	UINT stride = res->mVertexStride;
	UINT offset = 0;
	mContext->IASetVertexBuffers(0, 1, &res->mVertexBuffer, &stride, &offset);
	mContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	int start = 0;
	int count = res->mVertexCount;

	bool useIndex = res->mIndexBuffer != nullptr;
	if (useIndex)
	{
		DXGI_FORMAT format = DXGI_FORMAT_R16_UINT;
		switch (res->mIndexStride)
		{
		case 2: format = DXGI_FORMAT_R16_UINT; break;
		case 4: format = DXGI_FORMAT_R32_UINT; break;
		default:
			EXCEPT("unknown index format");
			break;
		}
		mContext->IASetIndexBuffer(res->mIndexBuffer, format, 0);

		count = res->mIndexCount;
	}

	res->mEffect->prepare(mContext);


	struct ViewPara
	{
		Vector3 eye;
		Vector3 at;
		Vector3 up;
		float width;
		float height;
		float depth;
	};

	const float scale = mScale / mVoxelSize;

	EffectParameter parameters;
	parameters.device = mDevice;
	parameters.context = mContext;
	parameters.world = mTranslation;
	parameters.proj = mProjection;
	parameters.width = range.x * scale;
	parameters.height = range.y * scale;
	parameters.depth = range.z * scale;




	//we need to render 3 times from different views
	const ViewPara views[] =
	{
		Vector3::ZERO, Vector3::UNIT_X, Vector3::UNIT_Y, -range.z, range.y, range.x,
		Vector3::ZERO, Vector3::NEGATIVE_UNIT_Y, Vector3::UNIT_Z, range.x, range.z, -range.y,
		Vector3::ZERO, Vector3::UNIT_Z , Vector3::UNIT_Y, range.x, range.y,range.z,
	};

	size_t arraysize = ARRAYSIZE(views);

	for (size_t i = 0; i < arraysize; ++i)
	{
		const ViewPara& v = views[i];
		const XMVECTOR Eye = XMVectorSet(v.eye.x, v.eye.y, v.eye.z, 0.0f);
		const XMVECTOR At = XMVectorSet(v.at.x, v.at.y, v.at.z, 0.0f);
		const XMVECTOR Up = XMVectorSet(v.up.x, v.up.y, v.up.z, 0.0f);

		parameters.view = XMMatrixTranspose(XMMatrixLookToLH(Eye, At, Up));
		parameters.viewport = i;

		Vector3 half = Vector3(v.width, v.height, v.depth) / 2;
		parameters.proj = XMMatrixTranspose(XMMatrixOrthographicOffCenterLH(
			-half.x, half.x, -half.y, half.y, -half.z, half.z));

		D3D11_VIEWPORT vp;
		vp.Width = abs(v.width) * scale;
		vp.Height = v.height * scale;
		vp.MinDepth = 0.0f;
		vp.MaxDepth = 1.0f;
		vp.TopLeftX = 0;
		vp.TopLeftY = 0;
		mContext->RSSetViewports(1, &vp);

		res->mEffect->update(parameters);

		if (useIndex)
			mContext->DrawIndexed(count, start, 0);
		else
			mContext->Draw(count, start);

	}

}

void Voxelizer::addEffect(Effect* effect)
{
	if (mEffects.insert(effect).second)
		effect->init(mDevice);
	
}

void Voxelizer::removeEffect(Effect* effect)
{
	auto ret = mEffects.find(effect);
	if (ret != mEffects.end())
	{
		(*ret)->clean();
		mEffects.erase(ret);
	}
}



VoxelResource* Voxelizer::createResource()
{
	VoxelResource* vr = new VoxelResource(mDevice);
	mResources.push_back(vr);
	return vr;
}

VoxelOutput* Voxelizer::createOutput()
{
	VoxelOutput* vo = new VoxelOutput(mDevice, mContext);
	mOutputs.push_back(vo);
	return vo;
}
