import PlayerComponent from "./player.vue";
import { BasePage } from "../../base-page.js";

class PagePlayer extends BasePage {
  constructor() {
    super();
  }

  onLoad(options) {
    super.onLoad(options);
    this.setRootComponent(PlayerComponent);
  }

  onNewOptions(options) {
    super.onNewOptions(options);
  }
}

export default PagePlayer;