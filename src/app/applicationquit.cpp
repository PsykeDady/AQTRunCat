#include "applicationquit.h"

#include <QCoreApplication>

void requestApplicationQuit()
{
    QCoreApplication::exit(0);
}
