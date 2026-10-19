#include "writeflow.h"

#include "dialogs/messagedialog.h"

bool stateMovedSince(QWidget* dialogParent, Repository& repository, const Repository::StateStamp& stamp)
{
	if (repository.stateMatches(stamp))
		return false;

	MessageDialog::notice(dialogParent, QObject::tr("Repository changed"),
		QObject::tr("The repository changed while the dialog was open, so nothing was done.\n"
		   "Review the new state and retry."), {});
	repository.refresh();
	return true;
}
