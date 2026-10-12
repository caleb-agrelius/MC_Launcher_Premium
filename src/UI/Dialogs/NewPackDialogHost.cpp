#include "UI/Dialogs/NewPackDialogHost.h"

#include "Services/Library/CustomPackService.h"

void NewPackDialogHost::Draw()
{
	NewPackDialogContext context;
	context.http = &m_rt.http;
	context.tasks.run = [&tasks = m_rt.tasks](std::function<void()> work) { tasks.Run(std::move(work)); };
	context.tasks.post = [&ui = m_rt.ui](std::function<void()> task) { ui.Post(std::move(task)); };
	NewPackRequest request;
	if (!m_dialog.Draw(context, request))
		return;
	CustomPackSpec spec;
	spec.name = request.name;
	spec.minecraftVersion = request.minecraftVersion;
	spec.loader = request.loader;
	spec.loaderVersion = request.loaderVersion;
	const std::string packId = m_packs.Create(spec);
	if (!packId.empty())
		m_navigator.ShowManage(packId, true);
}
