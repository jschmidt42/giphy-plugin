define(require => {
    'use strict';

    const _ = require('lodash');
    const stingray = require('stingray');
    const projectService = require('services/project-service');
    const engineService = require('services/engine-service');

    /**
     * Copy gif into project and compile.
     */
    function importGiphy (importOptions, previousResult, files, destination, flags) {
        if (!_.isArray(files))
            files = [files];

        console.log(importOptions, previousResult, files, destination, flags);

        return projectService.getCurrentProjectPath().then(function (projectPath) {
            let projectDestination = stingray.path.join(projectPath, destination);

            let importFile = sourceFilePath => {
                let fileName = stingray.path.basename(sourceFilePath, true);
                let fileDestination = stingray.path.join(projectDestination, fileName);
                return stingray.fs.copy(sourceFilePath, fileDestination);
            };

            return Promise.series(files.map(path => {
                if (stingray.fs.exists(path))
                    return path;
                return stingray.path.join(projectPath, path);
            }), importFile).then(() => engineService.enqueueDataCompile());
        });

    }

    return {
        importGiphy,
    };
});