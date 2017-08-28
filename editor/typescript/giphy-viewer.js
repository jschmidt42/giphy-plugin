define(function (require) {
    'use strict';

    const _ = require('lodash');
    const stingray = require('stingray');
    const m = require('components/mithril-ext');
    const ListView = require('components/list-view');
    const Toolbar = require('components/toolbar');
    const Textbox = require('components/textbox');
    const hostService = require('services/host-service');
    const giphyClient = require('./giphy-client');

    /**
     * Giphy manager to show searched giphy.
     */
    class GiphyViewer {

        /**
         * Construct the Giphy viewer.
         */
        constructor () {
            // Construct view elements:
            /**
             * Store all fetched Giphy.
             * @type {GiphyResult[]}
             */
            this.giphies = [];

            /**
             * Debounced function to search for new Giphy.
             * @type {Function}
             */
            this.search = _.debounce(q => giphyClient.search(q).then(result => this.showResult(result)), 200);
            /**
             * Create Giphy search query model.
             * This model getter/setter gets called each
             * time the model is updated are requested.
             */
            this.searchQuery = '';
            this.searchQueryModel = q => {
                if (!_.isNil(q)) {
                    this.searchQuery = q;
                    if (_.size(this.searchQuery) > 2) {
                        this.search(this.searchQuery);
                    }
                }
                return this.searchQuery;
            };
            /**
             * Toolbar items
             * @type {[*]}
             */
            this.toolbarItems = [
                { component: GiphyViewer.createSearchBox(this.searchQueryModel) },
                { img: 'arrows-refresh.svg', title: 'Search...', action: () => this.search(this.searchQuery) },
                { img: 'save.svg', title: 'Import Giphy frames (as PNGs)...', action: () => this.importFrames() }
            ];
            /**
             * Create the Giphy list view component.
             */
            this.giphyListView = GiphyViewer.createListView(() => this.giphies, [
                GiphyViewer.createColumn(m.column.name, 'name', 300, 'Name', m.dataType.string)
            ]);

            // Get trending results
            giphyClient.fetchTrending().then(result => this.showResult(result));
        }

        /**
         * Render the viewer with all the Giphy.
         */
        render () {
            return m.layout.vertical({}, [
                Toolbar.component({items: this.toolbarItems}),
                m('div', {className: "panel-fill"}, [
                    m('div', {className: "fullscreen stingray-border-dark"}, [
                        ListView.component(this.giphyListView)
                    ])
                ])
            ]);
        }

        /**
         * Refresh the entire view with new results.
         */
        refresh () {
            this.giphyListView.refresh();
        }

        /**
         * Push new results to the Giphy list view and refresh it.
         * @param {GiphyResult[]} result
         */
        showResult (result) {
            this.giphies = result;
            return this.refresh();
        }

        /**
         * Import all the frames of the selected Giphy.
         */
        importFrames () {
            let selectedGiphy = _.first(this.giphyListView.getSelection());
            if (!selectedGiphy)
                return Promise.reject('No Giphy selection');
            // Ask user where to save frames.
            return hostService.getFolder('Select where to save frames...', stingray.env.userDownloadDir)
                .then(folder => this.saveGiphy(selectedGiphy, folder))
                .then(savedFilePath => this.extractFrames(savedFilePath))
                .then(extractedFrameFilePaths => hostService.showInExplorer(extractedFrameFilePaths[0]))
                .catch(err => console.error(err));
        }

        /**
         * Download the original Giphy file on disk.
         * @param giphy
         * @param folder
         * @returns {*}
         */
        saveGiphy (giphy, folder) {
            if (!folder)
                return Promise.reject('Invalid folder');
            return giphyClient.download(giphy.id, giphy.url, folder);
        }

        /**
         * Call a C++ native function to extract all frames as png files.
         * @param filePath
         * @returns {*}
         */
        extractFrames (filePath) {
            // Dynamically load the native plugin DLL
            const nativePluginDllPath = require.toUrl('@giphy/binaries/editor/win64/dev/editor_plugin_w64_dev.dll');
            if (!stingray.fs.exists(nativePluginDllPath))
                throw new Error('Giphy editor native plugin does not exists at `' + nativePluginDllPath + '`. Was it compiled?');
            let pluginId = stingray.loadNativeExtension(nativePluginDllPath);
            // Call our native function.
            /** @namespace window.nativeGiphy */
            let paths = window.nativeGiphy.extractFrames(filePath);
            // We do not need the plugin anymore, let's dispose of it.
            stingray.unloadNativeExtension(pluginId);
            return paths;
        }

        /**
         * Entry point to compose view.
         */
        static view (ctrl) {
            return ctrl.render();
        }

        /**
         * Mount a new playground
         * @param {HtmlElement} container
         * @return {{component, noAngular: boolean}}
         */
        static mount (container) {
            let instance = m.mount(container, {
                controller: this,
                view: this.view
            });
            return { instance, component: this, noAngular: true };
        }

        /**
         * Create a search box component to do Giphy searches.
         * @param {function} searchModel - Functor to get and set the search query.
         */
        static createSearchBox (searchModel) {
            return Textbox.component({
                model: searchModel,
                focusMe: true,
                clearable: true,
                liveUpdate: true,
                placeholder: 'Enter search query...'
            });
        }

        /**
         * Create basic list view columns.
         */
        static createColumn (type, property, width, name, dataType = undefined, format = undefined, onClick = undefined) {
            return {
                type,
                dataType,
                property,
                format,
                onClick,
                uniqueId: property,
                defaultWidth: width,
                header: { text: name, tooltip: `Sort by ${name}`},
            };
        }

        /**
         * Create the Giphy list view
         */
        static createListView (items, columns) {
            return ListView.config({
                id: 'giphy-list-view',
                items: items,
                columns: columns,
                layoutOptions: ListView.toLayoutOptions({ size: 7, filter: '' }),
                tooltipProperty: 'name',
                typedNavigationProperty: '*',
                thumbnailProperty: "thumbnail",
                filterProperty: '*',
                defaultSort: {uniqueId: 'name', property: 'name', reverse: false},
                showLines: true,
                showHeader: true,
                showListThumbnails: true,
                allowMultiSelection: false,
                allowMousewheelResize: true
            });
        }
    }

    document.title = 'Giphy Viewer';

    // Initialize the application
    return GiphyViewer.mount($('.main-container')[0]);
});