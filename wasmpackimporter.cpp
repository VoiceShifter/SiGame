#include "wasmpackimporter.h"

#include <QDir>
#include <QHash>
#include <QObject>
#include <QPointer>

#include <utility>

#ifdef Q_OS_WASM
#include <emscripten.h>

namespace
{
struct ImportRequest
{
      QPointer<QObject> context;
      WasmPackImporter::Completion completion;
      QString successPath;
};

QHash<int, ImportRequest> requests;
int nextRequestId{1};
}

extern "C" EMSCRIPTEN_KEEPALIVE void sigameFileImportFinished(int requestId,
                                                              int status)
{
      const auto iterator = requests.find(requestId);
      if (iterator == requests.end())
      {
            return;
      }
      const ImportRequest request = iterator.value();
      requests.erase(iterator);
      if (request.context.isNull() || !request.completion)
      {
            return;
      }
      request.completion(status == 1 ? request.successPath : QString(),
                         status < 0);
}

EM_JS(void, sigameChooseLocalFiles, (int requestId, int packDirectory), {
      const input = document.createElement('input');
      input.type = 'file';
      if (packDirectory) {
            input.multiple = true;
            input.setAttribute('webkitdirectory', '');
      } else {
            input.accept = 'image/png,image/jpeg,image/bmp,image/gif,image/webp';
      }
      input.style.display = 'none';
      document.body.appendChild(input);

      let finished = false;
      let selectionStarted = false;
      const finish = (status) => {
            if (finished)
                  return;
            finished = true;
            input.remove();
            Module._sigameFileImportFinished(requestId, status);
      };

      input.addEventListener('cancel', () => finish(0));
      input.addEventListener('change', async () => {
            selectionStarted = true;
            const files = Array.from(input.files || []);
            if (files.length === 0) {
                  finish(0);
                  return;
            }

            try {
                  if (packDirectory) {
                        const root = '/tmp/sigame-pack-' + requestId;
                        FS.mkdirTree(root);
                        FS.mkdirTree(root + '/Audio');
                        FS.mkdirTree(root + '/Images');
                        FS.mkdirTree(root + '/Video');

                        const rawPaths = files.map(file =>
                              file.webkitRelativePath || file.name);
                        const firstPart = rawPaths[0].split('/')[0];
                        const stripRoot = rawPaths.every(path =>
                              path === firstPart ||
                              path.startsWith(firstPart + '/'));

                        for (let index = 0; index < files.length; ++index) {
                              let parts = rawPaths[index].split('/');
                              if (stripRoot)
                                    parts = parts.slice(1);
                              parts = parts.filter(part =>
                                    part.length > 0 && part !== '.');
                              if (parts.length === 0 || parts.includes('..'))
                                    throw new Error('Invalid relative pack path');

                              const destination = root + '/' + parts.join('/');
                              const slash = destination.lastIndexOf('/');
                              FS.mkdirTree(destination.substring(0, slash));
                              const contents = new Uint8Array(
                                    await files[index].arrayBuffer());
                              FS.writeFile(destination, contents);
                        }
                  } else {
                        const destination =
                              '/tmp/sigame-profile-' + requestId;
                        const contents = new Uint8Array(
                              await files[0].arrayBuffer());
                        FS.writeFile(destination, contents);
                  }
                  finish(1);
            } catch (error) {
                  console.error('Unable to import SiGame file', error);
                  finish(-1);
            }
      });

      window.addEventListener('focus', () => {
            window.setTimeout(() => {
                  if (!selectionStarted)
                        finish(0);
            }, 300);
      }, { once: true });
      input.click();
});

void beginImport(QObject *context, WasmPackImporter::Completion completion,
                 const QString &pathPrefix, bool packDirectory)
{
      if (context == nullptr || !completion)
      {
            return;
      }
      QDir().mkpath(QStringLiteral("/tmp"));
      const int requestId = nextRequestId++;
      const QString successPath =
            QStringLiteral("/tmp/%1-%2").arg(pathPrefix).arg(requestId);
      requests.insert(requestId,
                      {context, std::move(completion), successPath});
      sigameChooseLocalFiles(requestId, packDirectory ? 1 : 0);
}
#endif

void WasmPackImporter::choosePackDirectory(QObject *context,
                                           Completion completion)
{
#ifdef Q_OS_WASM
      beginImport(context, std::move(completion),
                  QStringLiteral("sigame-pack"), true);
#else
      Q_UNUSED(context)
      if (completion)
      {
            completion({}, true);
      }
#endif
}

void WasmPackImporter::chooseProfileImage(QObject *context,
                                          Completion completion)
{
#ifdef Q_OS_WASM
      beginImport(context, std::move(completion),
                  QStringLiteral("sigame-profile"), false);
#else
      Q_UNUSED(context)
      if (completion)
      {
            completion({}, true);
      }
#endif
}
